// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#define MEGA_BITS_PER_SECOND 1000000ull
#define USBFN_FULL_SPEED (12 * MEGA_BITS_PER_SECOND)
#define USBFN_HIGH_SPEED (480 * MEGA_BITS_PER_SECOND)
#define USBFN_SUPER_SPEED (5000 * MEGA_BITS_PER_SECOND)

typedef
_IRQL_always_function_max_(PASSIVE_LEVEL)
void
EVT_USBNCM_DEVICE_START_RECEIVE(
    _In_ WDFDEVICE usbNcmWdfDevice
);

typedef
_IRQL_always_function_max_(PASSIVE_LEVEL)
void
EVT_USBNCM_DEVICE_STOP_RECEIVE(
    _In_ WDFDEVICE usbNcmWdfDevice
);

typedef
_IRQL_always_function_max_(PASSIVE_LEVEL)
void
EVT_USBNCM_DEVICE_START_TRANSMIT(
    _In_ WDFDEVICE usbNcmWdfDevice
);

typedef
_IRQL_always_function_max_(PASSIVE_LEVEL)
void
EVT_USBNCM_DEVICE_STOP_TRANSMIT(
    _In_ WDFDEVICE usbNcmWdfDevice
);

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
EVT_USBNCM_DEVICE_TRANSMIT_FRAMES(
    _In_ WDFDEVICE usbNcmWdfDevice,
    _In_ TX_BUFFER_REQUEST * bufferRequest
);

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
void
EVT_USBNCM_ADAPTER_SET_LINK_STATE(
    _In_ NETADAPTER netAdapter,
    _In_ BOOLEAN connected
);

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
void
EVT_USBNCM_ADAPTER_SET_LINK_SPEED(
    _In_ NETADAPTER netAdapter,
    _In_ ULONG64 upLinkSpeed,
    _In_ ULONG64 downLinkSpeed
);

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
void
EVT_USBNCM_ADAPTER_NOTIFY_RECEIVE(
    _In_ NETADAPTER netAdapter,
    _In_reads_opt_(bufferSize) PUCHAR buffer,
    _In_opt_ size_t bufferSize,
    _In_opt_ WDFMEMORY bufferMemory,
    _In_opt_ WDFOBJECT returnContext
);

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
void
EVT_USBNCM_ADAPTER_NOTIFY_TRANSMIT_COMPLETION(
    _In_ NETADAPTER netAdapter,
    _In_ TX_BUFFER_REQUEST * bufferRequest
);

typedef struct _USBNCM_DEVICE_EVENT_CALLBACKS
{
    ULONG
        Size;

    EVT_USBNCM_DEVICE_START_RECEIVE *
        EvtUsbNcmStartReceive;

    EVT_USBNCM_DEVICE_STOP_RECEIVE *
        EvtUsbNcmStopReceive;

    EVT_USBNCM_DEVICE_START_TRANSMIT *
        EvtUsbNcmStartTransmit;

    EVT_USBNCM_DEVICE_STOP_TRANSMIT *
        EvtUsbNcmStopTransmit;

    EVT_USBNCM_DEVICE_TRANSMIT_FRAMES *
        EvtUsbNcmTransmitFrames;

} USBNCM_DEVICE_EVENT_CALLBACKS;

typedef struct _USBNCM_ADAPTER_EVENT_CALLBACKS
{
    ULONG
        Size;

    EVT_USBNCM_ADAPTER_SET_LINK_STATE *
        EvtUsbNcmAdapterSetLinkState;

    EVT_USBNCM_ADAPTER_SET_LINK_SPEED *
        EvtUsbNcmAdapterSetLinkSpeed;

    EVT_USBNCM_ADAPTER_NOTIFY_RECEIVE *
        EvtUsbNcmAdapterNotifyReceive;

    EVT_USBNCM_ADAPTER_NOTIFY_TRANSMIT_COMPLETION *
        EvtUsbNcmAdapterNotifyTransmitCompletion;

} USBNCM_ADAPTER_EVENT_CALLBACKS;

typedef struct _USBNCM_ADAPTER_PARAMETERS
{
    BOOLEAN
        Use32BitNtb;

    BYTE *
        MacAddress;

    UINT16
        MaxDatagramSize;

    UINT16
        TxMaxNtbDatagramCount;

    UINT32
        TxMaxNtbSize;

    UINT16
        TxNdpAlignment;

    UINT16
        TxNdpDivisor;

    UINT16
        TxNdpPayloadRemainder;

} USBNCM_ADAPTER_PARAMETERS;

PAGED
NTSTATUS
UsbNcmAdapterCreate(
    _In_ WDFDEVICE wdfDevice,
    _In_ USBNCM_ADAPTER_PARAMETERS const * parameters,
    _In_ USBNCM_DEVICE_EVENT_CALLBACKS const * usbNcmDeviceCallbacks,
    _Outptr_ NETADAPTER * netAdapter,
    _Outptr_ USBNCM_ADAPTER_EVENT_CALLBACKS const ** usbNcmAdapterCallbacks
);

PAGED
void
UsbNcmAdapterDestory(
    _In_ NETADAPTER netAdapter
);
