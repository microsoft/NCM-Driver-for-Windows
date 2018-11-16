// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

class NcmTxQueue
{

#pragma region NETADAPTER event call backs

public:

    PAGED
    static 
    NTSTATUS 
    EvtCreateTxQueue(
        _In_ NETADAPTER netAdapter,
        _Inout_ NETTXQUEUE_INIT * netTxQueueInit);

    _IRQL_requires_max_(DISPATCH_LEVEL)
    inline
    static
    void
    EvtSetNotificationEnabled(
        _In_ NETPACKETQUEUE netQueue,
        _In_ BOOLEAN notificationEnabled)
    {
        if (notificationEnabled)
        {
            // UsbNcm's tx is completed instantly within Advance()
            NetTxQueueNotifyMoreCompletedPacketsAvailable(netQueue);
        }
    }

    PAGED
    inline
    static
    void
    EvtCancel(_In_ NETPACKETQUEUE netQueue)
    {
        Get(netQueue)->Cancel();
    }

    _IRQL_requires_max_(DISPATCH_LEVEL)
    inline
    static
    void
    EvtAdvance(_In_ NETPACKETQUEUE netQueue)
    {
        Get(netQueue)->Advance();
    }

    PAGED
    inline
    static
    void
    EvtStart(_In_ NETPACKETQUEUE netQueue)
    {
        Get(netQueue)->Start();
    }

#pragma endregion

private:

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    NcmTxQueue* Get(_In_ NETPACKETQUEUE queue);

    PAGED
    static
    void
    EvtDestroyTxQueue(_In_ WDFOBJECT object);

    PAGED
    NcmTxQueue(_In_ NcmAdapter* ncmAdapter,
               _In_ NETPACKETQUEUE queue) :
        m_NcmAdapter(ncmAdapter),
        m_Queue(queue)
    {
        m_Rings = NetTxQueueGetRingCollection(m_Queue);
    }

    PAGED
    NTSTATUS
    InitializeQueue();

    _IRQL_requires_max_(DISPATCH_LEVEL)
    void Advance();

    PAGED
    void Cancel();

    PAGED
    void Start();

private:

    NET_RING_COLLECTION const *             m_Rings = nullptr;
    NcmAdapter*                             m_NcmAdapter = nullptr;
    NETPACKETQUEUE                          m_Queue = nullptr;
    NTB_HANDLE                              m_NtbHandle = nullptr;
    TX_BUFFER_REQUEST_POOL                  m_TxBufferRequestPool = nullptr;

    friend NcmAdapter;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NcmTxQueue, NcmGetTxQueueFromHandle);

