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

    NONPAGED
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

    PAGED
    inline
    static
    void
    EvtStop(_In_ NETPACKETQUEUE netQueue)
    {
        Get(netQueue)->Stop();
    }

#pragma endregion

private:

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    NcmTxQueue* Get(_In_ NETPACKETQUEUE queue);

    PAGED
    NcmTxQueue(_In_ NcmAdapter* ncmAdapter,
               _In_ NETPACKETQUEUE queue) :
        m_NcmAdapter(ncmAdapter),
        m_Queue(queue)
    {
        m_OsQueue.RingCollection = NetTxQueueGetRingCollection(m_Queue);

        NET_EXTENSION_QUERY extension;
        NET_EXTENSION_QUERY_INIT(
            &extension,
            NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_NAME,
            NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_VERSION_1,
            NetExtensionTypeFragment);

        NetTxQueueGetExtension(queue, &extension, &m_OsQueue.VirtualAddressExtension);
    }

    PAGED
    NTSTATUS
    InitializeQueue();

    _IRQL_requires_max_(DISPATCH_LEVEL)
    void Advance();

    NONPAGED
    void Cancel();

    PAGED
    void Start();

    PAGED
    void Stop();

private:

    NcmOsQueue              m_OsQueue;
    NcmAdapter*             m_NcmAdapter = nullptr;
    NETPACKETQUEUE          m_Queue = nullptr;
    NTB_HANDLE              m_NtbHandle = nullptr;
    TX_BUFFER_REQUEST_POOL  m_TxBufferRequestPool = nullptr;

    friend NcmAdapter;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NcmTxQueue, NcmGetTxQueueFromHandle);

