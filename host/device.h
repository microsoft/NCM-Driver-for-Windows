// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <new.h>
#include <usb.h>
#include <usbdlib.h>
#include <wdfusb.h>
#include "trace.h"
#include "ncm.h"
#include "buffers.h"
#include "callbacks.h"
#include "mac.h"

EXTERN_C_START

class UsbNcmHostDevice
{
public:

    PAGED
    static
    void
    StartReceive(
        _In_ WDFDEVICE usbNcmWdfDevice
    );

    PAGED
    static
    void
    StopReceive(
        _In_ WDFDEVICE usbNcmWdfDevice
    );

    PAGED
    static
    void
    StartTransmit(
        _In_ WDFDEVICE usbNcmWdfDevice
    );

    PAGED
    static
    void
    StopTransmit(
        _In_ WDFDEVICE usbNcmWdfDevice
    );

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    NTSTATUS
    TransmitFrames(
        _In_ WDFDEVICE usbNcmWdfDevice,
        _In_ TX_BUFFER_REQUEST * bufferRequest
    );

    PAGED
    UsbNcmHostDevice(
        _In_ WDFDEVICE wdfDevice
    )
        : m_WdfDevice(wdfDevice)
    {
    }

    PAGED
    NTSTATUS
    InitializeDevice(
        void
    );

    PAGED
    NTSTATUS
    CreateAdapter(
        void
    );

    PAGED
    void
    DestroyAdapter(
        void
    );

    PAGED
    NTSTATUS
    EnterWorkingState(
        _In_ WDF_POWER_DEVICE_STATE previousState
    );

    PAGED
    NTSTATUS
    LeaveWorkingState(
        void
    );

private:

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    VOID
    ControlInterruptPipeReadCompletetionRoutine(
        _In_ WDFUSBPIPE pipe,
        _In_ WDFMEMORY memory,
        _In_ size_t numBytesTransfered,
        _In_ WDFCONTEXT context
    );

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    VOID
    DataBulkInPipeReadCompletetionRoutine(
        _In_ WDFUSBPIPE pipe,
        _In_ WDFMEMORY memory,
        _In_ size_t numBytesTransfered,
        _In_ WDFCONTEXT context
    );

    _IRQL_requires_max_(DISPATCH_LEVEL)
    static
    VOID
    TransmitFramesCompetion(
        _In_ WDFREQUEST request,
        _In_ WDFIOTARGET target,
        _In_ PWDF_REQUEST_COMPLETION_PARAMS params,
        _In_ WDFCONTEXT context
    );

    PAGED
    NTSTATUS
    RequestClassSpecificControlTransfer(
        _In_ UINT8 request,
        _In_ WDF_USB_BMREQUEST_DIRECTION direction,
        _In_ WDF_USB_BMREQUEST_RECIPIENT recipient,
        _In_ UINT16 value,
        _In_ PWDF_MEMORY_DESCRIPTOR memoryDescriptor
    );

    PAGED
    NTSTATUS
    SetDeviceFriendlyName(
        void
    );

    PAGED
    NTSTATUS
    SelectConfiguration(
        void
    );

    PAGED
    NTSTATUS
    SelectSetting(
        void
    );

    PAGED
    NTSTATUS
    RetrieveInterruptPipe(
        void
    );

    PAGED
    NTSTATUS
    RetrieveDataBulkPipes(
        void
    );

private:

    static
    USBNCM_DEVICE_EVENT_CALLBACKS const
        s_NcmDeviceCallbacks;

    WDFDEVICE
        m_WdfDevice = nullptr;

    NETADAPTER
        m_NetAdapter = nullptr;

    WDFUSBDEVICE
        m_WdfUsbTargetDevice = nullptr;

    ULONG
        m_UsbDeviceTraits;

    WDFUSBINTERFACE
        m_ControlInterface = nullptr;

    WDFUSBINTERFACE
        m_DataInterface = nullptr;

    WDFUSBPIPE
        m_ControlInterruptPipe = nullptr;

    WDFUSBPIPE
        m_DataBulkInPipe = nullptr;

    WDFUSBPIPE
        m_DataBulkOutPipe = nullptr;

    ULONG
        m_ControlInterruptPipeMaxPacket = 0;

    ULONG
        m_DataBulkOutPipeMaximumPacketSize = 0;

    BYTE
        m_MacAddress[ETH_LENGTH_OF_ADDRESS] = {};

    NTB_PARAMETERS
        m_NtbParamters;

    BOOLEAN
        m_Use32BitNtb = FALSE;

    UINT16
        m_MaxDatagramSize = 0;

    UINT32
        m_HostSelectedNtbInMaxSize = 0;

    USBNCM_ADAPTER_EVENT_CALLBACKS const *
        m_NcmAdapterCallbacks;

};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UsbNcmHostDevice, NcmGetHostDeviceFromHandle)

EXTERN_C_END
