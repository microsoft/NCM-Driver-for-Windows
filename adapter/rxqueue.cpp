// Copyright (C) Microsoft Corporation. All rights reserved.

#include "adapter.h"
#include "rxqueue.tmh"

_Use_decl_annotations_
NcmRxQueue* NcmRxQueue::Get(_In_ NETPACKETQUEUE queue)
{
    return NcmGetRxQueueFromHandle(queue);
}

PAGED
_Use_decl_annotations_
NTSTATUS
NcmRxQueue::EvtCreateRxQueue(
    _In_ NETADAPTER netAdapter,
    _Inout_ NETRXQUEUE_INIT * netRxQueueInit)
{
    NET_PACKET_QUEUE_CONFIG queueConfig;
    WDF_OBJECT_ATTRIBUTES rxQueueAttributes;
    NETPACKETQUEUE queue;

    PAGED_CODE();

    NcmAdapter* ncmAdapter = NcmGetAdapterFromHandle(netAdapter);

    NET_PACKET_QUEUE_CONFIG_INIT(&queueConfig,
                                 EvtAdvance,
                                 EvtSetNotificationEnabled,
                                 EvtCancel);

    queueConfig.EvtStart = EvtStart;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&rxQueueAttributes,
                                            NcmRxQueue);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        NetRxQueueCreate(netRxQueueInit,
                         &rxQueueAttributes,
                         &queueConfig,
                         &queue),
        "NetRxQueueCreate failed");

    //
    // Use the inplacement new and invoke the constructor on the
    // context memory space allocated for queue instance
    //

    NcmRxQueue* rxQueue = 
        new (NcmGetRxQueueFromHandle(queue)) NcmRxQueue(ncmAdapter,
                                                        queue);

    NCM_RETURN_IF_NOT_NT_SUCCESS(rxQueue->InitializeQueue());

    return STATUS_SUCCESS;
}

PAGED
_Use_decl_annotations_
NTSTATUS
NcmRxQueue::InitializeQueue()
{
    NCM_RETURN_IF_NOT_NT_SUCCESS(
        RxBufferQueueCreate(m_NcmAdapter->m_WdfDevice,
                            m_Queue,
                            &m_RxBufferQueue));

    NCM_RETURN_IF_NOT_NT_SUCCESS(
        NcmTransferBlockCreate(m_Queue,
                               m_NcmAdapter->m_Parameters.Use32BitNtb,
                               m_NcmAdapter->m_Parameters.TxMaxNtbDatagramCount,
                               m_NcmAdapter->m_Parameters.TxNdpAlignment,
                               m_NcmAdapter->m_Parameters.TxNdpDivisor,
                               m_NcmAdapter->m_Parameters.TxNdpPayloadRemainder,
                               &m_NtbHandle));

    m_NcmAdapter->m_RxQueue = this;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
void NcmRxQueue::Advance()
{
    NET_RING_PACKET_ITERATOR pi = NetRingGetAllPackets(m_Rings);
    NET_RING_FRAGMENT_ITERATOR fi = NetRingGetAllFragments(m_Rings);

    while (NetFragmentIteratorHasAny(&fi))
    {
        PUCHAR frame = nullptr;
        size_t frameSize = 0;

        if (STATUS_SUCCESS == GetNextFrame(&frame, &frameSize))
        {
            NT_FRE_ASSERT(frame != nullptr);
            NT_FRE_ASSERT(frameSize > 0);

            NET_FRAGMENT* fragment = NetFragmentIteratorGetFragment(&fi);
            fragment->Offset = 0;
            fragment->ValidLength = frameSize;
            RtlCopyMemory(fragment->VirtualAddress, frame, frameSize);

            NET_PACKET* packet = NetPacketIteratorGetPacket(&pi);
            packet->FragmentIndex = fi.Iterator.Index;
            packet->FragmentCount = 1;

            NetPacketIteratorAdvance(&pi);
            NetFragmentIteratorAdvance(&fi);
        }
        else
        {
            break;
        }
    }

    NetFragmentIteratorSet(&fi);
    NetPacketIteratorSet(&pi);
}

_Use_decl_annotations_
bool
NcmRxQueue::MatchPacketFilter(
    _In_reads_bytes_(frameSize) UCHAR const* frame,
    _In_ size_t frameSize)
{
    UINT8 const* header = (UINT8 const*) frame;
    bool match = false;

    if (*header & 1)
    {
        if (*header == 0xff)
        {
            if (m_NcmAdapter->m_PacketFilters & (NDIS_PACKET_TYPE_BROADCAST | NDIS_PACKET_TYPE_PROMISCUOUS))
            {
                //broacast
                match = true;
                m_NcmAdapter->m_Stats.ifHCInBroadcastPkts++;
                m_NcmAdapter->m_Stats.ifHCInBroadcastOctets += frameSize;
            }
        }
        else if (m_NcmAdapter->m_PacketFilters & (NDIS_PACKET_TYPE_ALL_MULTICAST | NDIS_PACKET_TYPE_PROMISCUOUS))
        {
            //multicast
            match = true;
            m_NcmAdapter->m_Stats.ifHCInMulticastPkts++;
            m_NcmAdapter->m_Stats.ifHCInMulticastOctets += frameSize;
        }
    }
    else if (((m_NcmAdapter->m_PacketFilters & NDIS_PACKET_TYPE_PROMISCUOUS) != 0) ||
              (((m_NcmAdapter->m_PacketFilters & NDIS_PACKET_TYPE_DIRECTED) != 0) &&
               (RtlEqualMemory(header, m_NcmAdapter->m_CurrentMacAddress.Address, ETH_LENGTH_OF_ADDRESS))))
    {
        //unicast
        match = true;
        m_NcmAdapter->m_Stats.ifHCInUcastPkts++;
        m_NcmAdapter->m_Stats.ifHCInUcastOctets += frameSize;
    }

    return match;
}


_Use_decl_annotations_
NTSTATUS
NcmRxQueue::GetNextFrame(
    _Outptr_result_buffer_(frameSize) PUCHAR* frame,
    _Out_ size_t* frameSize)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    *frame = nullptr;
    *frameSize = 0;

    while (true)
    {
        if (m_RxBufferInProcessing == nullptr)
        {
            status = RxBufferQueueDequeueBuffer(m_RxBufferQueue,
                                                &m_RxBufferInProcessing);

            if (NT_SUCCESS(status))
            {
                status = NcmTransferBlockReInitializeBuffer(m_NtbHandle, 
                                                            m_RxBufferInProcessing->Buffer, 
                                                            m_RxBufferInProcessing->BufferSize, 
                                                            NTB_RX);

                if (NT_SUCCESS(status))
                {
                    // Valid NTB and now go ahead to start processing datagrams inside
                }
                else
                {
                    // Invalid NTB, skip
                    RxBufferQueueReturnBuffer(m_RxBufferQueue,
                                              m_RxBufferInProcessing);
                    m_RxBufferInProcessing = nullptr;

                    continue;
                }
            }
            else
            {
                // no more NTB, return
                status = STATUS_NO_MORE_ENTRIES;
                break;
            }
        }

        if (STATUS_SUCCESS == NcmTransferBlockGetNextDatagram(m_NtbHandle, frame, frameSize))
        {
            if (*frameSize < sizeof(ETHERNET_HEADER) ||
                *frameSize > m_NcmAdapter->m_Parameters.MaxDatagramSize)
            {
                // bad datagram, skip
                m_NcmAdapter->m_Stats.ifInErrors++;
                continue;
            }

            if (MatchPacketFilter(*frame, *frameSize))
            {
                // valid datagram, return
                status = STATUS_SUCCESS;
                break;
            }
            else
            {
                //filtered;
                continue;
            }
        }
        else
        {
            // no more datagarms from current NTB, get the next NTB
            RxBufferQueueReturnBuffer(m_RxBufferQueue,
                                      m_RxBufferInProcessing);
            m_RxBufferInProcessing = nullptr;

            continue;
        }
    }

    return status;
}

PAGED
_Use_decl_annotations_
void NcmRxQueue::Start()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStartReceive(m_NcmAdapter->GetWdfDevice());
}

PAGED
_Use_decl_annotations_
void NcmRxQueue::Cancel()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStopReceive(m_NcmAdapter->GetWdfDevice());

    NET_RING_PACKET_ITERATOR pi = NetRingGetAllPackets(m_Rings);
    NetPacketIteratorAdvanceToTheEnd(&pi);
    NetPacketIteratorSet(&pi);

    NET_RING_FRAGMENT_ITERATOR fi = NetRingGetAllFragments(m_Rings);
    NetFragmentIteratorAdvanceToTheEnd(&fi);
    NetFragmentIteratorSet(&fi);
}
