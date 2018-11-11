// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <new.h>
#include "trace.h"
#include "ncm.h"
#include "buffers.h"
#include "callbacks.h"
#include "mac.h"

EXTERN_C_START

#include <kusbfnclasslib.h>

struct KUSBFNCLASSLIB_CONTEXT;

VOID
UsbFnKmClassLibHelperPreProcessBusEventSubscription(
    __in PUSBFN_NOTIFICATION pNotification,
    __in KUSBFNCLASSLIB_CONTEXT* pContext
);

EXTERN_C_END

enum NcmRequestType
{
    NcmReqSetEthMcastFilts = USB_REQUEST_SET_ETHERNET_MULTICAST_FILTERS,
    NcmReqSetEthPwrMgmtFilt = USB_REQUEST_SET_ETHERNET_PWR_MGMT_FILTER,
    NcmReqGetEthPwrMgmtFilt = USB_REQUEST_GET_ETHERNET_PWR_MGMT_FILTER,
    NcmReqSetEthPacketFilt = USB_REQUEST_SET_ETHERNET_PACKET_FILTER,
    NcmReqGetEthStat = USB_REQUEST_GET_ETHERNET_STATISTIC,
    NcmReqGetNtbParams = USB_REQUEST_GET_NTB_PARAMETERS,
    NcmReqGetNetAddr = USB_REQUEST_GET_NET_ADDRESS,
    NcmReqSetNetAddr = USB_REQUEST_SET_NET_ADDRESS,
    NcmReqGetNtbFmt = USB_REQUEST_GET_NTB_FORMAT,
    NcmReqSetNtbFmt = USB_REQUEST_SET_NTB_FORMAT,
    NcmReqGetNtbInputSize = USB_REQUEST_GET_NTB_INPUT_SIZE,
    NcmReqSetNtbInputSize = USB_REQUEST_SET_NTB_INPUT_SIZE,
    NcmReqGetMaxDatagramSize = USB_REQUEST_GET_MAX_DATAGRAM_SIZE,
    NcmReqSetMaxDatagramSize = USB_REQUEST_SET_MAX_DATAGRAM_SIZE,
    NcmReqGetCrcMode = USB_REQUEST_GET_CRC_MODE,
    NcmReqSetCrcMode = USB_REQUEST_SET_CRC_MODE,
};

static UINT64 g_UsbFnBusSpeed[] = { 0, USBFN_FULL_SPEED, USBFN_HIGH_SPEED, USBFN_SUPER_SPEED, (UINT32) -1 };

class UsbNcmFunctionDevice
{
public:

    PAGED
    static
    void
    StartReceive(_In_ WDFDEVICE usbNcmWdfDevice);

    PAGED
    static
    void
    StopReceive(_In_ WDFDEVICE usbNcmWdfDevice);

    PAGED
    static
    void
    StartTransmit(_In_ WDFDEVICE usbNcmWdfDevice);

    PAGED
    static
    void
    StopTransmit(_In_ WDFDEVICE usbNcmWdfDevice);

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    NTSTATUS
    TransmitFrames(_In_ WDFDEVICE usbNcmWdfDevice,
                   _In_ TX_BUFFER_REQUEST* bufferRequest);

    PAGED
    UsbNcmFunctionDevice(_In_ WDFDEVICE wdfDevice) :
        m_WdfDevice(wdfDevice) {}

    PAGED
    NTSTATUS
    InitializeDevice();

    PAGED
    void
    UnInitializeDevice();

    PAGED
    NTSTATUS
    SubscribeBusEventNotification();

    PAGED
    VOID
    UnSubscribeBusEventNotification();

private:

    PAGED
    static
    void
    GetPipeMemoryForDataReader(
        _In_ DMFMODULE dmfModule,
        _Out_writes_(*inputBufferSize) VOID* inputBuffer,
        _Out_ size_t* inputBufferSize,
        _In_ VOID*);

    PAGED
    static
    ContinuousRequestTarget_BufferDisposition
    DataBulkOutPipeRead(_In_ DMFMODULE dmfModule,
                        _In_reads_(OutputBufferSize) VOID* outputBuffer,
                        _In_ size_t outputBufferSize,
                        _In_ VOID* clientBufferContextOutput,
                        _In_ NTSTATUS completionStatus);

    PAGED
    static
    ContinuousRequestTarget_BufferDisposition
    BusEventNotificationRead(_In_ DMFMODULE dmfModule,
                             _In_reads_(OutputBufferSize) VOID* outputBuffer,
                             _In_ size_t outputBufferSize,
                             _In_ VOID* clientBufferContextOutput,
                             _In_ NTSTATUS completionStatus);

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    VOID
    TransmitFramesCompetion(_In_ WDFREQUEST request,
                            _In_ WDFIOTARGET target,
                            _In_ PWDF_REQUEST_COMPLETION_PARAMS params,
                            _In_ WDFCONTEXT context);

    PAGED
    NTSTATUS
    CacheClassInformation();

    PAGED
    void
    SetParameters();

    PAGED
    NTSTATUS
    RegisterCdcMacString();

    PAGED
    NTSTATUS
    CreateContinuousReaders();

    PAGED
    VOID
    InterruptHost(
        _In_ PVOID InterruptBuffer,
        _In_ size_t InterruptBufferSize);

    PAGED
    VOID
    NotifyConnectionSpeedAndStatusChange(
        _In_ bool NetworkConnectionState,
        _In_ UINT64 LinkSpeed);

    PAGED
    NTSTATUS
    SendHandshake();

    PAGED
    NTSTATUS
    ReadFromEndPoint0(
        _In_ size_t bytesToRead,
        _Out_writes_bytes_(bytesToRead) PVOID buffer);

    PAGED
    NTSTATUS
    WriteToEndPoint0(
        _In_reads_bytes_(writeBufLen) PVOID writeBuf,
        _In_ size_t writeBufLen);

    PAGED
    VOID
    StallEndPoint0();

    PAGED
    VOID
    ProcessSetupPacket(
        _In_ const USB_DEFAULT_PIPE_SETUP_PACKET& SetupPacket);

    PAGED
    NTSTATUS
    CreateAdapter();

    PAGED
    VOID
    DestroyAdapter();

private:

    static const
    USBNCM_DEVICE_EVENT_CALLBACKS           s_NcmDeviceCallbacks;

    WDFDEVICE                               m_WdfDevice = nullptr;
    NETADAPTER                              m_NetAdapter = nullptr;

    USBNCM_ADAPTER_EVENT_CALLBACKS const*   m_NcmAdapterCallbacks = nullptr;

    KUSBFNCLASSLIBHANDLE                    m_UsbFnClassLibHandle = nullptr;

    USBFN_DEVICE_STATE                      m_State = UsbfnDeviceStateMinimum;
    USBFN_BUS_SPEED                         m_BusSpeed = UsbfnBusSpeedLow;

    // Interfaces and Endpoints
    USHORT                                  m_AlternateInterfaceNumber = 0;
    UINT8                                   m_CommunicationInterfaceIndex = 0;
    UINT8                                   m_DataInterfaceIndex = 0;

    USBFN_PIPE_INFORMATION                  m_EndPoint0 = {};
    USBFN_PIPE_INFORMATION                  m_Interrupt = {};
    USBFN_PIPE_INFORMATION                  m_BulkIn = {};
    USBFN_PIPE_INFORMATION                  m_BulkOut = {};

    // NCM
    BYTE                                    m_CdcMacAddress[ETH_LENGTH_OF_ADDRESS] = {};
    BOOLEAN                                 m_Use32BitNtb = FALSE;
    NTB_PARAMETERS                          m_NtbParamters = {};
    UINT32                                  m_HostSelectedNtbInMaxSize = 0;

    EX_RUNDOWN_REF                          m_BulkInRundown = {};

    DMFMODULE                               m_ControlContinuousRequest = nullptr;
    DMFMODULE                               m_DataContinuousRequest = nullptr;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UsbNcmFunctionDevice, NcmGetFunctionDeviceFromHandle)
