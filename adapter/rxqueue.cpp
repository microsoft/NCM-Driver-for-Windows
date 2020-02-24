// Copyright (C) Microsoft Corporation. All rights reserved.

#include "adapter.h"
#include "rxqueue.tmh"

_Use_decl_annotations_
NcmRxQueue* NcmRxQueue::Get(_In_ NETPACKETQUEUE queue)
{
    return NcmGetRxQueueFromHandle(queue);
}

PAGEDX
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
    queueConfig.EvtStop = EvtStop;

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

PAGEDX
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
    NcmPacketIterator pi = NcmGetAllPackets(&m_OsQueue);
    NcmFragmentIterator fi = NcmGetAllFragments(&m_OsQueue);

    while (fi.HasAny())
    {
        PUCHAR frame = nullptr;
        size_t frameSize = 0;

        if (STATUS_SUCCESS == GetNextFrame(&frame, &frameSize))
        {
            NT_FRE_ASSERT(frame != nullptr);
            NT_FRE_ASSERT(frameSize > 0);

            NET_FRAGMENT* fragment = fi.GetFragment();
            fragment->Offset = 0;
            fragment->ValidLength = frameSize;
            RtlCopyMemory(fi.GetVirtualAddress()->VirtualAddress, frame, frameSize);

            NET_PACKET* packet = pi.GetPacket();
            packet->FragmentIndex = fi.GetIndex();
            packet->FragmentCount = 1;
            packet->Layout = {};

            pi.Advance();
            fi.Advance();
        }
        else
        {
            break;
        }
    }

    pi.Set();
}

_Use_decl_annotations_
bool
NcmRxQueue::MatchPacketFilter(
    UINT8 const* frame) const
{
    bool match = false;

    if (*frame & 1)
    {
        if (*frame == 0xff)
        {
            if (m_NcmAdapter->m_PacketFilters & (NDIS_PACKET_TYPE_BROADCAST | NDIS_PACKET_TYPE_PROMISCUOUS))
            {
                //broacast
                match = true;
            }
        }
        else if (m_NcmAdapter->m_PacketFilters & (NDIS_PACKET_TYPE_ALL_MULTICAST | NDIS_PACKET_TYPE_PROMISCUOUS))
        {
            //multicast
            match = true;
        }
    }
    else if (((m_NcmAdapter->m_PacketFilters & NDIS_PACKET_TYPE_PROMISCUOUS) != 0) ||
              (((m_NcmAdapter->m_PacketFilters & NDIS_PACKET_TYPE_DIRECTED) != 0) &&
               (RtlEqualMemory(frame, m_NcmAdapter->m_CurrentMacAddress.Address, ETH_LENGTH_OF_ADDRESS))))
    {
        //unicast
        match = true;
    }

    return match;
}

NTSTATUS
NcmRxQueue::GetNextFrame(
    _Outptr_result_buffer_(*frameSize) PUCHAR* frame,
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
                continue;
            }

            if (MatchPacketFilter(*frame))
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

PAGEDX
_Use_decl_annotations_
void NcmRxQueue::Start()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStartReceive(m_NcmAdapter->GetWdfDevice());
}

PAGEDX
_Use_decl_annotations_
void NcmRxQueue::Stop()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStopReceive(m_NcmAdapter->GetWdfDevice());
}

NONPAGEDX
_Use_decl_annotations_
void NcmRxQueue::Cancel()
{
    NcmReturnAllPacketsAndFragments(&m_OsQueue);
}
