// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <new.h>

#include <netadaptercx.h>
#include <preview/netringiterator.h>
#include <NetPacketLibrary.h>

#include "trace.h"
#include "ncm.h"
#include "ntb.h"
#include "buffers.h"
#include "callbacks.h"

class NcmAdapter;

#include "rxqueue.h"
#include "txqueue.h"

class NcmAdapter
{

public:

    PAGED
    NTSTATUS
    StartAdapter();

    PAGED
    NTSTATUS
    ConfigAdapter();

    WDFDEVICE
    GetWdfDevice() { return m_WdfDevice; }

    PAGED
    static
    void
    HandleQueryRequest(
        _In_ NETREQUESTQUEUE requestQueue,
        _In_ NETREQUEST request,
        _Out_writes_bytes_(outputBufferLength) PVOID outputBuffer,
        _In_ UINT outputBufferLength);

    PAGED
    static
    void
    SetPacketFilter(
        _In_ NETREQUESTQUEUE requestQueue,
        _In_ NETREQUEST request,
        _In_reads_bytes_(inputBufferLength) PVOID inputBuffer,
        _In_ UINT inputBufferLength);

private:

    PAGED
    static
    NTSTATUS
    PreviewWakePacket(
        _In_ NETADAPTER netAdapter,
        _In_ NETPOWERSETTINGS existingWakeSettings,
        _In_ NDIS_PM_WOL_PACKET woLPacketType,
        _In_ PNDIS_PM_WOL_PATTERN patternToBeAdded);

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    void
    SetLinkState(
        _In_ NETADAPTER netAdapter,
        _In_ BOOLEAN linkUp);

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    void
    SetLinkSpeed(_In_ NETADAPTER netAdapter,
                 _In_ ULONG64 UpLinkSpeed,
                 _In_ ULONG64 DownLinkSpeed);

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    void
    NotifyReceive(_In_ NETADAPTER netAdapter,
                  _In_reads_opt_(bufferSize) PUCHAR buffer,
                  _In_opt_ size_t bufferSize,
                  _In_opt_ WDFMEMORY bufferMemory,
                  _In_opt_ WDFOBJECT returnContext);

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    void
    NotifyTransmitCompletion(
        _In_ NETADAPTER netAdapter,
        _In_ TX_BUFFER_REQUEST* bufferRequest);

    PAGED
    NcmAdapter(_In_ WDFDEVICE wdfDevice,
               _In_ USBNCM_ADAPTER_PARAMETERS const* parameters,
               _In_ USBNCM_DEVICE_EVENT_CALLBACKS const* usbNcmCallbacks,
               _In_ NETADAPTER adapter) :
        m_NetAdapter(adapter),
        m_Parameters(*parameters),
        m_UsbNcmDeviceCallbacks(usbNcmCallbacks),
        m_WdfDevice(wdfDevice)
    {
        NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&m_PermanentMacAddress,
                                            ETH_LENGTH_OF_ADDRESS,
                                            m_Parameters.MacAddress);
    }

    PAGED
    NTSTATUS
    ConfigNetRequestQueue();

private:

    static const USBNCM_ADAPTER_EVENT_CALLBACKS s_NcmAdapterCallbacks;
    WDFDEVICE                                   m_WdfDevice = nullptr;
    NETADAPTER                                  m_NetAdapter = nullptr;
    NcmTxQueue*                                 m_TxQueue = nullptr;
    NcmRxQueue*                                 m_RxQueue = nullptr;
    NET_ADAPTER_LINK_LAYER_ADDRESS              m_PermanentMacAddress;
    NET_ADAPTER_LINK_LAYER_ADDRESS              m_CurrentMacAddress;
    USBNCM_DEVICE_EVENT_CALLBACKS const*        m_UsbNcmDeviceCallbacks = nullptr;
    bool                                        m_LinkUp = false;
    ULONG64                                     m_TxLinkSpeed = 0;
    ULONG64                                     m_RxLinkSpeed = 0;
    bool                                        m_S0IdleWakeCapable = false;
    USBNCM_ADAPTER_PARAMETERS const             m_Parameters = {};
    NDIS_STATISTICS_INFO                        m_Stats = {};
    NET_PACKET_FILTER_TYPES_FLAGS               m_PacketFilters = (NET_PACKET_FILTER_TYPES_FLAGS) 0x0;

    friend NcmTxQueue;
    friend NcmRxQueue;

    PAGED
    friend
    NTSTATUS
    UsbNcmAdapterCreate(
        _In_ WDFDEVICE wdfDevice,
        _In_ USBNCM_ADAPTER_PARAMETERS const* parameters,
        _In_ USBNCM_DEVICE_EVENT_CALLBACKS const* usbNcmDeviceCallbacks,
        _Outptr_ NETADAPTER* ncmAdapter,
        _Outptr_ USBNCM_ADAPTER_EVENT_CALLBACKS const** usbNcmAdapterCallbacks);
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NcmAdapter, NcmGetAdapterFromHandle);

