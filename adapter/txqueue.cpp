// Copyright (C) Microsoft Corporation. All rights reserved.

#include "adapter.h"
#include "txqueue.tmh"

_Use_decl_annotations_
inline
NcmTxQueue* NcmTxQueue::Get(_In_ NETPACKETQUEUE queue)
{
    return NcmGetTxQueueFromHandle(queue);
}

PAGEDX
_Use_decl_annotations_
NTSTATUS
NcmTxQueue::EvtCreateTxQueue(
    NETADAPTER netAdapter,
    NETTXQUEUE_INIT * netTxQueueInit)
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
    queueConfig.EvtStop = EvtStop;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&txQueueAttributes,
                                            NcmTxQueue);

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

PAGEDX
_Use_decl_annotations_
NTSTATUS
NcmTxQueue::InitializeQueue()
{
    NCM_RETURN_IF_NOT_NT_SUCCESS(
        TxBufferRequestPoolCreate(m_NcmAdapter->m_WdfDevice,
                                  m_Queue,
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
    NcmPacketIterator pi = NcmGetAllPackets(&m_OsQueue);

    while (pi.HasAny())
    {
        TX_BUFFER_REQUEST* bufferRequest = nullptr;

        NTSTATUS status =
            TxBufferRequestPoolGetBufferRequest(m_TxBufferRequestPool,
                                                &bufferRequest);

        if (NT_SUCCESS(status))
        {
            (void) NcmTransferBlockReInitializeBuffer(m_NtbHandle,
                                                      bufferRequest->Buffer,
                                                      bufferRequest->BufferLength,
                                                      NTB_TX);

            while (pi.HasAny())
            {
                if (STATUS_SUCCESS != NcmTransferBlockCopyNextDatagram(m_NtbHandle, &pi))
                {
                    // no more space in the NTB for further datagram, send the current NTB now
                    break;
                }

                // tx is completed instantly once packet is copied
                pi.Advance();
            }

            NcmTransferBlockSetNdp(m_NtbHandle, &bufferRequest->TransferLength);

            status =
                m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmTransmitFrames(
                    m_NcmAdapter->GetWdfDevice(),
                    bufferRequest);

            if (!NT_SUCCESS(status))
            {
                TxBufferRequestPoolReturnBufferRequest(m_TxBufferRequestPool,
                                                       bufferRequest);
            }
        }
        else
        {
            // no tx buffer available, drop and complete this packet
            pi.Advance();
        }

        pi.Set();
    }
}

PAGEDX
void NcmTxQueue::Start()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStartTransmit(m_NcmAdapter->GetWdfDevice());
}

PAGEDX
void NcmTxQueue::Stop()
{
    (void) m_NcmAdapter->m_UsbNcmDeviceCallbacks->EvtUsbNcmStopTransmit(m_NcmAdapter->GetWdfDevice());
}

NONPAGEDX
void NcmTxQueue::Cancel()
{
    NcmReturnAllPacketsAndFragments(&m_OsQueue);
}
