// Copyright (C) Microsoft Corporation. All rights reserved.

#include "adapter.h"
#include "adapter.tmh"

#define NCM_SUPPORTED_PACKET_FILTERS (         \
            NetPacketFilterFlagDirected      | \
            NetPacketFilterFlagMulticast     | \
            NetPacketFilterFlagAllMulticast  | \
            NetPacketFilterFlagBroadcast     | \
            NetPacketFilterFlagPromiscuous)

#define NCM_MAX_MCAST_LIST 1

const
USBNCM_ADAPTER_EVENT_CALLBACKS
NcmAdapter::s_NcmAdapterCallbacks =
{
    sizeof(USBNCM_ADAPTER_EVENT_CALLBACKS),
    NcmAdapter::SetLinkState,
    NcmAdapter::SetLinkSpeed,
    NcmAdapter::NotifyReceive,
    NcmAdapter::NotifyTransmitCompletion,
};

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmAdapterCreate(
    _In_ WDFDEVICE wdfDevice,
    _In_ USBNCM_ADAPTER_PARAMETERS const * parameters,
    _In_ USBNCM_DEVICE_EVENT_CALLBACKS const * usbNcmCallbacks,
    _Outptr_ NETADAPTER * netAdapter,
    _Outptr_ USBNCM_ADAPTER_EVENT_CALLBACKS const ** usbNcmAdapterCallbacks
)
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attribs;
    NcmAdapter * ncmAdapter = nullptr;

    PAGED_CODE();

    NETADAPTER_INIT * adapterInit = NetAdapterInitAllocate(wdfDevice);

    *netAdapter = WDF_NO_HANDLE;

    NET_ADAPTER_DATAPATH_CALLBACKS datapathCallbacks;
    NET_ADAPTER_DATAPATH_CALLBACKS_INIT(
        &datapathCallbacks,
        NcmTxQueue::EvtCreateTxQueue,
        NcmRxQueue::EvtCreateRxQueue);

    NetAdapterInitSetDatapathCallbacks(
        adapterInit,
        &datapathCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attribs,
        NcmAdapter);

    status = NetAdapterCreate(adapterInit, &attribs, netAdapter);

    NetAdapterInitFree(adapterInit);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(status, "NetAdapterCreate failed");

    // Use the inplacement new and invoke the constructor on the
    // context memory space allocated for Adapter instance
    ncmAdapter =
        new (NcmGetAdapterFromHandle(*netAdapter)) NcmAdapter(
            wdfDevice,
            parameters,
            usbNcmCallbacks,
            *netAdapter);

    *usbNcmAdapterCallbacks = &NcmAdapter::s_NcmAdapterCallbacks;

    NCM_RETURN_IF_NOT_NT_SUCCESS(ncmAdapter->ConfigAdapter());

    NCM_RETURN_IF_NOT_NT_SUCCESS(ncmAdapter->StartAdapter());

    return STATUS_SUCCESS;
}

PAGEDX
_Use_decl_annotations_
void
UsbNcmAdapterDestory(
    _In_ NETADAPTER netAdapter
)
{
    PAGED_CODE();

    NetAdapterStop(netAdapter);
    WdfObjectDelete(netAdapter);
}

PAGEDX
_Use_decl_annotations_
NTSTATUS
NcmAdapter::ConfigAdapter(
    void
)
{
    NTSTATUS status = STATUS_SUCCESS;
    NETCONFIGURATION configuration = WDF_NO_HANDLE;
    bool currMacExisted = false;

    PAGED_CODE();

    status = NetAdapterOpenConfiguration(
        m_NetAdapter,
        WDF_NO_OBJECT_ATTRIBUTES,
        &configuration);

    if (NT_SUCCESS(status))
    {
        // Read NetworkAddress registry value and use it as the current address
        status = NetConfigurationQueryLinkLayerAddress(
            configuration,
            &m_CurrentMacAddress);

        if (NT_SUCCESS(status))
        {
            currMacExisted = true;
        }

        NetConfigurationClose(configuration);
    }

    if (!currMacExisted)
    {
        TraceInfo(
            USBNCM_ADAPTER,
            "no current mac address found in registry, use permanent mac address");

        m_CurrentMacAddress = m_PermanentMacAddress;
    }

    return STATUS_SUCCESS;
}

PAGEDX
void
EvtSetReceiveFilter(
    _In_ NETADAPTER NetAdapter,
    _In_ NETRECEIVEFILTER Handle
)
{
    NcmAdapter * ncmAdapter = NcmGetAdapterFromHandle(NetAdapter);

    NET_PACKET_FILTER_FLAGS PacketFilter = NetReceiveFilterGetPacketFilter(Handle);
    ncmAdapter->SetPacketFilter(PacketFilter);
}

PAGEDX
_Use_decl_annotations_
NTSTATUS
NcmAdapter::StartAdapter(
    void
)
{
    NET_ADAPTER_LINK_LAYER_CAPABILITIES linkLayerCaps;
    NET_ADAPTER_LINK_STATE currentLinkState;

    NET_ADAPTER_TX_CAPABILITIES txCaps;
    NET_ADAPTER_RX_CAPABILITIES rxCaps;

    PAGED_CODE();

    // TODO: replace magic number below with what the NIC should report
    // according to NCM spec.

    NET_ADAPTER_LINK_LAYER_CAPABILITIES_INIT(
        &linkLayerCaps,
        USBFN_SUPER_SPEED,
        USBFN_SUPER_SPEED);

    NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES receiveFilterCapabilities;
    NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES_INIT(
        &receiveFilterCapabilities,
        EvtSetReceiveFilter);
    receiveFilterCapabilities.SupportedPacketFilters = NCM_SUPPORTED_PACKET_FILTERS;
    receiveFilterCapabilities.MaximumMulticastAddresses = NCM_MAX_MCAST_LIST;

    NetAdapterSetLinkLayerCapabilities(m_NetAdapter, &linkLayerCaps);
    NetAdapterSetPermanentLinkLayerAddress(m_NetAdapter, &m_PermanentMacAddress);
    NetAdapterSetCurrentLinkLayerAddress(m_NetAdapter, &m_CurrentMacAddress);
    NetAdapterSetLinkLayerMtuSize(
        m_NetAdapter,
        m_Parameters.MaxDatagramSize - sizeof(ETHERNET_HEADER));
    NetAdapterSetReceiveFilterCapabilities(m_NetAdapter, &receiveFilterCapabilities);

    // Specify the current link state
    NET_ADAPTER_LINK_STATE_INIT(
        &currentLinkState,
        NDIS_LINK_SPEED_UNKNOWN,
        MediaConnectStateUnknown,
        MediaDuplexStateFull,
        NetAdapterPauseFunctionTypeUnsupported,
        NetAdapterAutoNegotiationFlagDuplexAutoNegotiated);

    NetAdapterSetLinkState(m_NetAdapter, &currentLinkState);

    // datapath capabilities
    NET_ADAPTER_TX_CAPABILITIES_INIT(&txCaps, 1);
    NET_ADAPTER_RX_CAPABILITIES_INIT_SYSTEM_MANAGED(&rxCaps, m_Parameters.MaxDatagramSize, 1);

    NetAdapterSetDataPathCapabilities(m_NetAdapter, &txCaps, &rxCaps);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        NetAdapterStart(m_NetAdapter),
        "NetAdapterStart failed");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
void
NcmAdapter::SetPacketFilter(
    _In_ NET_PACKET_FILTER_FLAGS PacketFilter
)
{
    m_PacketFilters = PacketFilter;
}

_Use_decl_annotations_
void
NcmAdapter::SetLinkState(
    _In_ NETADAPTER netAdapter,
    _In_ BOOLEAN linkUp
)
{
    NcmAdapter * ncmAdapter = NcmGetAdapterFromHandle(netAdapter);

    NET_ADAPTER_LINK_STATE currentLinkState;

    if (linkUp)
    {
        ncmAdapter->m_LinkUp = true;
        NET_ADAPTER_LINK_STATE_INIT(
            &currentLinkState,
            0,
            MediaConnectStateConnected,
            MediaDuplexStateFull,
            NetAdapterPauseFunctionTypeUnsupported,
            NetAdapterAutoNegotiationFlagDuplexAutoNegotiated);

        currentLinkState.TxLinkSpeed = ncmAdapter->m_TxLinkSpeed;
        currentLinkState.RxLinkSpeed = ncmAdapter->m_RxLinkSpeed;
    }
    else
    {
        ncmAdapter->m_LinkUp = false;
        NET_ADAPTER_LINK_STATE_INIT(
            &currentLinkState,
            0,
            MediaConnectStateDisconnected,
            MediaDuplexStateUnknown,
            NetAdapterPauseFunctionTypeUnsupported,
            NetAdapterAutoNegotiationFlagDuplexAutoNegotiated);
    }

    NetAdapterSetLinkState(ncmAdapter->m_NetAdapter, &currentLinkState);
}

_Use_decl_annotations_
void
NcmAdapter::SetLinkSpeed(
    _In_ NETADAPTER netAdapter,
    _In_ ULONG64 UpLinkSpeed,
    _In_ ULONG64 DownLinkSpeed
)
{
    NcmAdapter * ncmAdapter = NcmGetAdapterFromHandle(netAdapter);

    ncmAdapter->m_TxLinkSpeed = UpLinkSpeed;
    ncmAdapter->m_RxLinkSpeed = DownLinkSpeed;
}

_Use_decl_annotations_
inline
void
NcmAdapter::NotifyReceive(
    _In_ NETADAPTER netAdapter,
    _In_reads_opt_(bufferSize) PUCHAR buffer,
    _In_opt_ size_t bufferSize,
    _In_opt_ WDFMEMORY bufferMemory,
    _In_opt_ WDFOBJECT returnContext
)
{
    NcmAdapter * ncmAdapter = NcmGetAdapterFromHandle(netAdapter);

    RxBufferQueueEnqueueBuffer(
        ncmAdapter->m_RxQueue->m_RxBufferQueue,
        buffer,
        bufferSize,
        bufferMemory,
        returnContext);

    ncmAdapter->m_RxQueue->NotifyReceive();
}


_Use_decl_annotations_
inline
void
NcmAdapter::NotifyTransmitCompletion(
    _In_ NETADAPTER netAdapter,
    _In_ TX_BUFFER_REQUEST * bufferRequest
)
{
    NcmAdapter * ncmAdapter = NcmGetAdapterFromHandle(netAdapter);

    TxBufferRequestPoolReturnBufferRequest(
        ncmAdapter->m_TxQueue->m_TxBufferRequestPool,
        bufferRequest);

    // UsbNcm's tx is completed instantly within Advance()
    // no need to notify OS about tx completion
}
