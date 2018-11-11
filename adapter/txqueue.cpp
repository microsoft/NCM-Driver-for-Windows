// Copyright (C) Microsoft Corporation. All rights reserved.

#include "adapter.h"
#include "txqueue.tmh"

_Use_decl_annotations_
inline
NcmTxQueue* NcmTxQueue::Get(_In_ NETPACKETQUEUE queue)
{
    return NcmGetTxQueueFromHandle(queue);
}

PAGED
_Use_decl_annotations_
void
NcmTxQueue::EvtDestroyTxQueue(
    _In_ WDFOBJECT object)
{
    // TODO once DMF support attach non-WDFDEVICE as the parent object
    // we don't need to do this anymore

    NcmTxQueue* txQueue = NcmGetTxQueueFromHandle(object);

    if (txQueue->m_TxBufferRequestPool != nullptr)
    {
        WdfObjectDelete(txQueue->m_TxBufferRequestPool);
        txQueue->m_TxBufferRequestPool = nullptr;
    }
}

PAGED
_Use_decl_annotations_
NTSTATUS
NcmTxQueue::EvtCreateTxQueue(
    _In_ NETADAPTER netAdapter,
    _Inout_ NETTXQUEUE_INIT * netTxQueueInit)
{
    NET_PACKET_QUEUE_CONFIG queueConfig;
    WDF_OBJECT_ATTRIBUTES txQueueAttributes;
    NETPACKETQUEUE queue;

    PAGED_CODE();

    NcmAdapter* ncmAdapter = NcmGetAdapterFromHandle(netAdapter);

    NET_PACKET_QUEUE_CONFIG_INIT(&queueConfig,
                                 EvtAdvance,
                                 EvtSetNotificationEnabled,
                                 EvtCancel);

    queueConfig.EvtStart = EvtStart;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&txQueueAttributes,
                                            NcmTxQueue);
    txQueueAttributes.EvtDestroyCallback = NcmTxQueue::EvtDestroyTxQueue;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        NetTxQueueCreate(netTxQueueInit,
                         &txQueueAttributes,
                         &queueConfig,
                         &queue),
        "NetTxQueueCreate failed");

    //
    // Use the inplacement new and invoke the constructor on the
    // context memory space allocated for queue instance
    //

    NcmTxQueue* txQueue = 
        new (NcmGetTxQueueFromHandle(queue)) NcmTxQueue(ncmAdapter,
                                                        queue);

    NCM_RETURN_IF_NOT_NT_SUCCESS(txQueue->InitializeQueue());

    return STATUS_SUCCESS;
}

PAGED
_Use_decl_annotations_
NTSTATUS
NcmTxQueue::InitializeQueue()
{
    NCM_RETURN_IF_NOT_NT_SUCCESS(
        TxBufferRequestPoolCreate(m_NcmAdapter->m_WdfDevice,
                                  m_NcmAdapter->m_Parameters.TxMaxNtbSize,
                                  &m_TxBufferRequestPool));

    NCM_RETURN_IF_NOT_NT_SUCCESS(
        NcmTransferBlockCreate(m_Queue,
                               m_NcmAdapter->m_Parameters.Use32BitNtb,
                               m_NcmAdapter->m_Parameters.TxMaxNtbDatagramCount,
                               m_NcmAdapter->m_Parameters.TxNdpAlignment,
                               m_NcmAdapter->m_Parameters.TxNdpDivisor,
                               m_NcmAdapter->m_Parameters.TxNdpPayloadRemainder,
                               &m_NtbHandle));

    m_NcmAdapter->m_TxQueue = this;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
void NcmTxQueue::Advance()
{
    NET_RING_PACKET_ITERATOR pi = NetRingGetTxPostPacketIterator(m_Rings);

    while (NetRingIteratorAny(pi))
    {
        TX_BUFFER_REQUEST* bufferRequest = nullptr;
        NDIS_STATISTICS_INFO Stats = {};

        NTSTATUS status =
            TxBufferRequestPoolGetBufferRequest(m_TxBufferRequestPool,
                                                &bufferRequest);

        if (NT_SUCCESS(status))
        {
            (void) NcmTransferBlockReInitializeBuffer(m_NtbHandle,
                                                      bufferRequest->Buffer,
                                                      bufferRequest->BufferLength,
                                                      NTB_TX);

            while (NetRingIteratorAny(pi))
            {
                NET_PACKET * txNetPacket = NetRingIteratorGetPacket(&pi);

                if (STATUS_SUCCESS != NcmTransferBlockSetNextDatagram(m_NtbHandle, &pi, &Stats))
                {
                    // no more space in the NTB for further datagram, send the current NTB now
                    break;
                }

                // Use Scratch field as completion flag for the tx packet
                txNetPacket->Scratch = 1;
                NetRingAdvancePacketIterator(&pi);
            }

            NcmTransferBlockSetNdp(m_NtbHandle, &bufferRequest->TransferLength);

            status =
                m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmTransmitFrames(
                    m_NcmAdapter->GetWdfDevice(),
                    bufferRequest);

            if (NT_SUCCESS(status))
            {
                m_NcmAdapter->m_Stats.ifHCOutBroadcastPkts += Stats.ifHCOutBroadcastPkts;
                m_NcmAdapter->m_Stats.ifHCOutBroadcastOctets += Stats.ifHCOutBroadcastOctets;
                m_NcmAdapter->m_Stats.ifHCOutMulticastPkts += Stats.ifHCOutMulticastPkts;
                m_NcmAdapter->m_Stats.ifHCOutMulticastOctets += Stats.ifHCOutMulticastOctets;
                m_NcmAdapter->m_Stats.ifHCOutUcastPkts += Stats.ifHCOutUcastPkts;
                m_NcmAdapter->m_Stats.ifHCOutUcastOctets += Stats.ifHCOutUcastOctets;
            }
            else
            {
                TxBufferRequestPoolReturnBufferRequest(m_TxBufferRequestPool,
                                                       bufferRequest);

                m_NcmAdapter->m_Stats.ifOutErrors +=
                    Stats.ifHCOutBroadcastPkts +
                    Stats.ifHCOutMulticastPkts +
                    Stats.ifHCOutUcastPkts;
            }
        }
        else
        {
            // no tx buffer available, drop;
            m_NcmAdapter->m_Stats.ifOutDiscards++;

            NET_PACKET * txNetPacket = NetRingIteratorGetPacket(&pi);

            // Use Scratch field as completion flag for the tx packet
            txNetPacket->Scratch = 1;
            NetRingAdvancePacketIterator(&pi);
        }

        NetRingSetTxPostPacketIterator(&pi);
    }

    CompleteTxPacketsBatch(m_Rings, 1);
}

PAGED
_Use_decl_annotations_
void NcmTxQueue::Start()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStartTransmit(m_NcmAdapter->GetWdfDevice());
}

PAGED
_Use_decl_annotations_
void NcmTxQueue::Cancel()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStopTransmit(m_NcmAdapter->GetWdfDevice());

    NET_RING_PACKET_ITERATOR pi = NetRingGetAllPacketIterator(m_Rings);
    NetRingAdvanceEndPacketIterator(&pi);
    NetRingSetAllPacketIterator(&pi);

    NET_RING_FRAGMENT_ITERATOR fi = NetRingGetAllFragmentIterator(m_Rings);
    NetRingAdvanceEndFragmentIterator(&fi);
    NetRingSetAllFragmentIterator(&fi);
}
