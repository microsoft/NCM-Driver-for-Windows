// Copyright (C) Microsoft Corporation. All rights reserved.

#include "driver.h"
#include "device.tmh"

#define MAX_HOST_NTB_SIZE       (0x10000)
#define PENDING_BULK_IN_READS   (3)

const USBNCM_DEVICE_EVENT_CALLBACKS UsbNcmFunctionDevice::s_NcmDeviceCallbacks = {
    sizeof(USBNCM_DEVICE_EVENT_CALLBACKS),
    UsbNcmFunctionDevice::StartReceive,
    UsbNcmFunctionDevice::StopReceive,
    UsbNcmFunctionDevice::StartTransmit,
    UsbNcmFunctionDevice::StopTransmit,
    UsbNcmFunctionDevice::TransmitFrames
};

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::InitializeDevice()
{
    USBFN_CLASS_INFORMATION_PACKET_EX classInformation = { 0 };

    PAGED_CODE();

    NCM_RETURN_IF_NOT_NT_SUCCESS(
        UsbFnKmClassLibRegisterClassDevice(m_WdfDevice, &m_UsbFnClassLibHandle));

    NCM_RETURN_IF_NOT_NT_SUCCESS(CacheClassInformation());

    SetParameters();

    NCM_RETURN_IF_NOT_NT_SUCCESS(RegisterCdcMacString());

    ExInitializeRundownProtection(&m_BulkInRundown);

    NCM_RETURN_IF_NOT_NT_SUCCESS(CreateContinuousReaders());

    NCM_RETURN_IF_NOT_NT_SUCCESS(UsbFnKmClassLibActivateBus(m_UsbFnClassLibHandle));

    return STATUS_SUCCESS;
}

PAGEDX
_Use_decl_annotations_
void
UsbNcmFunctionDevice::UnInitializeDevice()
{
    PAGED_CODE();

    (void) UsbFnKmClassLibDeactivateBus(m_UsbFnClassLibHandle);

    UsbFnKmClassLibUnregisterClassDevice(m_UsbFnClassLibHandle);
}

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::CreateContinuousReaders()
{
    WDF_OBJECT_ATTRIBUTES objectAttributes;
    DMF_MODULE_ATTRIBUTES moduleAttributes;
    DMF_CONFIG_ContinuousRequestTarget moduleConfig;

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
    objectAttributes.ParentObject = m_WdfDevice;

    DMF_CONFIG_ContinuousRequestTarget_AND_ATTRIBUTES_INIT(&moduleConfig,
                                                           &moduleAttributes);
    moduleAttributes.PassiveLevel = true;

    moduleConfig.BufferContextInputSize = 0;
    moduleConfig.BufferContextOutputSize = 0;
    moduleConfig.BufferInputSize = 0x0;
    moduleConfig.BufferOutputSize = sizeof(USBFN_NOTIFICATION);
    moduleConfig.BufferCountInput = 0;
    moduleConfig.BufferCountOutput = 1;
    moduleConfig.ContinuousRequestCount = 1;
    moduleConfig.ContinuousRequestTargetIoctl = IOCTL_INTERNAL_USBFN_BUS_EVENT_NOTIFICATION;
    moduleConfig.ContinuousRequestTargetMode = ContinuousRequestTarget_Mode_Manual;
    moduleConfig.PoolTypeInput = NonPagedPoolNx;
    moduleConfig.PoolTypeOutput = NonPagedPoolNx;
    moduleConfig.RequestType = ContinuousRequestTarget_RequestType_InternalIoctl;
    moduleConfig.EvtContinuousRequestTargetBufferOutput = UsbNcmFunctionDevice::BusEventNotificationRead;
    moduleConfig.PurgeAndStartTargetInD0Callbacks = false;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        DMF_ContinuousRequestTarget_Create(m_WdfDevice,
                                           &moduleAttributes,
                                           &objectAttributes,
                                           &m_ControlContinuousRequest),
        "DMF_ContinuousRequestTarget_Create failed");

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
    objectAttributes.ParentObject = m_WdfDevice;

    DMF_CONFIG_ContinuousRequestTarget_AND_ATTRIBUTES_INIT(&moduleConfig,
                                                           &moduleAttributes);

    moduleAttributes.PassiveLevel = true;

    moduleConfig.BufferContextInputSize = 0;
    moduleConfig.BufferContextOutputSize = 0;
    moduleConfig.BufferInputSize = sizeof(USBFNPIPEID);
    moduleConfig.BufferOutputSize = m_NtbParamters.dwNtbOutMaxSize;
    moduleConfig.BufferCountInput = PENDING_BULK_IN_READS;
    moduleConfig.BufferCountOutput = 16;
    moduleConfig.ContinuousRequestCount = PENDING_BULK_IN_READS;
    moduleConfig.ContinuousRequestTargetIoctl = IOCTL_INTERNAL_USBFN_TRANSFER_OUT;
    moduleConfig.ContinuousRequestTargetMode = ContinuousRequestTarget_Mode_Manual;
    moduleConfig.PoolTypeInput = NonPagedPoolNx;
    moduleConfig.PoolTypeOutput = NonPagedPoolNx;
    moduleConfig.RequestType = ContinuousRequestTarget_RequestType_InternalIoctl;
    moduleConfig.EnableLookAsideOutput = true;
    moduleConfig.EvtContinuousRequestTargetBufferOutput = UsbNcmFunctionDevice::DataBulkOutPipeRead;
    moduleConfig.EvtContinuousRequestTargetBufferInput = UsbNcmFunctionDevice::GetPipeMemoryForDataReader;
    moduleConfig.PurgeAndStartTargetInD0Callbacks = false;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        DMF_ContinuousRequestTarget_Create(m_WdfDevice,
                                           &moduleAttributes,
                                           &objectAttributes,
                                           &m_DataContinuousRequest),
        "DMF_ContinuousRequestTarget_Create failed");

    return STATUS_SUCCESS;
}

#pragma region UsbNcm Fn Properties

//
// UsbNcm Fn Properties
//

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::CacheClassInformation()
{
    USBFN_CLASS_INFORMATION_PACKET_EX usbNcmFnClassInfo;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        UsbFnKmClassLibGetClassInformationEx(m_UsbFnClassLibHandle,
                                             &usbNcmFnClassInfo),
        "UsbFnKmClassLibGetClassInformationEx failed");

    //
    // NCM function should have the same information for all three different speeds
    // Pick SuperSpeedClass member field
    //
    UINT8 cached = 0;

    NT_FRE_ASSERT(usbNcmFnClassInfo.SuperSpeedClassInterfaceEx.InterfaceCount == 2);

    m_CommunicationInterfaceIndex = usbNcmFnClassInfo.SuperSpeedClassInterfaceEx.BaseInterfaceNumber;
    m_DataInterfaceIndex = m_CommunicationInterfaceIndex + 1;

    for (size_t i = 0; i < usbNcmFnClassInfo.SuperSpeedClassInterfaceEx.PipeCount; i++)
    {
        USBFN_PIPE_INFORMATION& pipeInfo = usbNcmFnClassInfo.SuperSpeedClassInterfaceEx.PipeArr[i];

        if (i == 0) //Endpoint 0 is always the first
        {
            m_EndPoint0 = pipeInfo;
            cached++;
        }
        else
        {
            if ((pipeInfo.EpDesc.bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK)
            {
                if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.EpDesc.bEndpointAddress))
                {
                    m_BulkIn = pipeInfo;
                    cached++;
                }
                else
                {
                    m_BulkOut = pipeInfo;
                    cached++;
                }
            }
            else if ((pipeInfo.EpDesc.bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT
                     &&
                     USB_ENDPOINT_DIRECTION_IN(pipeInfo.EpDesc.bEndpointAddress))
            {
                m_Interrupt = pipeInfo;
                cached++;
            }
        }
    }

    NT_FRE_ASSERT(cached == usbNcmFnClassInfo.SuperSpeedClassInterfaceEx.PipeCount);

    return STATUS_SUCCESS;
}

PAGEDX
_Use_decl_annotations_
VOID
UsbNcmFunctionDevice::SetParameters()
{
    //
    // NTB parameters for this function adapter
    //
    RtlZeroMemory(&m_NtbParamters, sizeof(m_NtbParamters));
    static_assert((sizeof(m_NtbParamters) == 0x1C), "NTB Parameters size doesn't match NCM spec-ed size, check the spec-ed size and typedef in code");

    m_NtbParamters.wLength = sizeof(m_NtbParamters);
    m_NtbParamters.bmNtbFormatsSupported = 0x0003;

    // NCM spec allows different max in vs out sizes.

    // IN
    m_NtbParamters.dwNtbInMaxSize = 0x10000;
    m_NtbParamters.wNdpInAlignment = 0x0004;
    m_NtbParamters.wNdpInDivisor = 0x0004;
    m_NtbParamters.wNdpInPayloadRemainder = 0x0;

    m_HostSelectedNtbInMaxSize = m_NtbParamters.dwNtbInMaxSize;

    // OUT
    m_NtbParamters.dwNtbOutMaxSize = 0x10000;
    m_NtbParamters.wNdpOutAlignment = 0x0004;
    m_NtbParamters.wNdpOutDivisor = 0x0004;
    m_NtbParamters.wNdpOutPayloadRemainder = 0x0;
    m_NtbParamters.wNtbOutMaxDatagrams = 0x0000;

    m_Use32BitNtb = 0;
}

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::RegisterCdcMacString()
{
    USBFN_USB_STRING usbfnMacAddressString;
    NTSTATUS status = STATUS_SUCCESS;
    NETCONFIGURATION configuration = nullptr;
    UNICODE_STRING cdcMacAddress = RTL_CONSTANT_STRING(L"CdcMacAddress");
    bool macExisted = false;

    PAGED_CODE();

    status = NetDeviceOpenConfiguration(m_WdfDevice,
                                        WDF_NO_OBJECT_ATTRIBUTES,
                                        &configuration);

    if (NT_SUCCESS(status))
    {
        WDFMEMORY macMemory = nullptr;

        status = NetConfigurationQueryBinary(configuration,
                                             &cdcMacAddress,
                                             PagedPool,
                                             WDF_NO_OBJECT_ATTRIBUTES,
                                             &macMemory);

        if (NT_SUCCESS(status))
        {
            PVOID value;
            size_t valueSize;

            value = WdfMemoryGetBuffer(macMemory, &valueSize);

            if (valueSize == ETH_LENGTH_OF_ADDRESS)
            {
                RtlCopyMemory(m_CdcMacAddress, value, valueSize);
                macExisted = true;
            }
        }
    }

    if (!macExisted)
    {
        TraceInfo(USBNCM_ADAPTER,
                  "no CDC mac address found in registry, generate a pseudo mac address");

        GenerateMACAddress(m_CdcMacAddress);

        if (configuration != nullptr)
        {
            NetConfigurationAssignBinary(configuration,
                                         &cdcMacAddress,
                                         m_CdcMacAddress,
                                         ETH_LENGTH_OF_ADDRESS);
        }
    }

    if (configuration != nullptr)
    {
        NetConfigurationClose(configuration);
    }

    RtlZeroMemory(&usbfnMacAddressString, sizeof(usbfnMacAddressString));
    usbfnMacAddressString.StringIndex = USB_ECM_MAC_STRING_INDEX;

    BytesToHexString(m_CdcMacAddress,
                     sizeof(WCHAR) * ETH_LENGTH_OF_ADDRESS,
                     usbfnMacAddressString.UsbString);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        UsbFnKmClassLibRegisterString(m_UsbFnClassLibHandle,
                                      &usbfnMacAddressString),
        "UsbFnKmClassLibRegisterString failed");

    return STATUS_SUCCESS;
}

#pragma endregion

#pragma region UsbNcm Fn Interrupt

//
// UsbNcm Fn Interrupt
//

PAGEDX
_Use_decl_annotations_
VOID
UsbNcmFunctionDevice::InterruptHost(
    PVOID InterruptBuffer,
    size_t InterruptBufferSize)
{
    WDFMEMORY memory = nullptr;
    PVOID buffer = nullptr;
    NTSTATUS status = STATUS_SUCCESS;

    // this memory is deleted in completion routine
    status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES,
                             NonPagedPoolNx,
                             0,
                             InterruptBufferSize,
                             &memory,
                             (PVOID*) &buffer);

    if (NT_SUCCESS(status))
    {
        RtlCopyMemory(buffer, InterruptBuffer, InterruptBufferSize);

        auto completionRoutine = [](WDFREQUEST request, WDFIOTARGET, PWDF_REQUEST_COMPLETION_PARAMS params, WDFCONTEXT)
        {
            TraceInfo(USBNCM_FUNCTION, "Interrupt completed,  %!status!", WdfRequestGetStatus(request));

            WdfObjectDelete(params->Parameters.Ioctl.Output.Buffer);
        };

        WDFREQUEST request;

        if (InterruptBufferSize % m_Interrupt.EpDesc.wMaxPacketSize)
        {
            status = UsbFnKmClassLibCreateTransferInRequest(m_UsbFnClassLibHandle,
                                                            m_Interrupt.PipeId,
                                                            memory,
                                                            completionRoutine,
                                                            &request);
        }
        else
        {
            status = UsbFnKmClassLibCreateTransferInAppendZlpRequest(m_UsbFnClassLibHandle,
                                                                     m_Interrupt.PipeId,
                                                                     memory,
                                                                     completionRoutine,
                                                                     &request);
        }

        if (NT_SUCCESS(status))
        {
            if (!WdfRequestSend(request, WdfDeviceGetIoTarget(m_WdfDevice), WDF_NO_SEND_OPTIONS))
            {
                status = WdfRequestGetStatus(request);
                WdfObjectDelete(memory);
                WdfObjectDelete(request);
            }
        }
        else
        {
            WdfObjectDelete(memory);
        }
    }

    TraceInfo(USBNCM_FUNCTION, "Send interrupt asynchronously,  %!status!", status);
}

PAGEDX
_Use_decl_annotations_
VOID
UsbNcmFunctionDevice::NotifyConnectionSpeedAndStatusChange(
    bool NetworkConnectionState,
    UINT64 LinkSpeed)
{
    if (NetworkConnectionState == true)
    {
        CDC_CONN_SPEED_CHANGE speedChange =
        {
            0xA1,
            USB_CDC_NOTIFICATION_CONNECTION_SPEED_CHANGE,
            0,
            m_DataInterfaceIndex,
            8,
            (UINT32) LinkSpeed,
            (UINT32) LinkSpeed
        };

        TraceInfo(USBNCM_FUNCTION, "Notify host about speed change");

        InterruptHost(&speedChange,
                      sizeof(speedChange));
    }

    CDC_NETWORK_CONNECTION_STATUS statusChange =
    {
        0xA1,
        USB_CDC_NOTIFICATION_NETWORK_CONNECTION,
        NetworkConnectionState,
        m_DataInterfaceIndex,
        0
    };

    TraceInfo(USBNCM_FUNCTION, "Notify host about status change");

    InterruptHost(&statusChange,
                  sizeof(statusChange));

    return;
}

#pragma endregion

#pragma region UsbNcm Fn Setup Packet Processing

//
// UsbNcm Fn Setup Packet Processing
//

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::SendHandshake()
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFREQUEST request;

    status = UsbFnKmClassLibCreateControlStatusHandshakeRequest(m_UsbFnClassLibHandle,
                                                                nullptr,
                                                                &request);
    if (NT_SUCCESS(status))
    {
        if (!WdfRequestSend(request, WdfDeviceGetIoTarget(m_WdfDevice), WDF_NO_SEND_OPTIONS))
        {
            status = WdfRequestGetStatus(request);
            WdfObjectDelete(request);
        }
    }

    TraceInfo(USBNCM_FUNCTION, "Send handshake to ep0 asynchronously,  %!status!", status);

    return status;
}

//
//Caller is responsible for the life time of the buffer & correct sizing of the buffer,
//ReadEp0 should always followed by a IOCTL_INTERNAL_USBFN_CONTROL_STATUS_HANDSHAKE_IN on ep0
//
PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::ReadFromEndPoint0(
    size_t bytesToRead,
    PVOID buffer)
{
    NTSTATUS status =
        DMF_ContinuousRequestTarget_SendSynchronously(
            m_ControlContinuousRequest,
            &m_EndPoint0.PipeId,
            sizeof(USBFNPIPEID),
            buffer,
            bytesToRead,
            ContinuousRequestTarget_RequestType_InternalIoctl,
            IOCTL_INTERNAL_USBFN_TRANSFER_OUT,
            0,
            nullptr);

    TraceInfo(USBNCM_FUNCTION, "Read from ep0 completed, %!status!", status);

    if (NT_SUCCESS(status))
    {
        SendHandshake();
    }

    return status;
}

//
//Caller is responsible for the life time of the buffer & correct sizing of the buffer,
//WriteEp0 should always followed by a IOCTL_INTERNAL_USBFN_CONTROL_STATUS_HANDSHAKE_OUT on ep0
//
PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::WriteToEndPoint0(
    PVOID writeBuf,
    size_t writeBufLen)
{
    NTSTATUS status =
        DMF_ContinuousRequestTarget_SendSynchronously(
            m_ControlContinuousRequest,
            &m_EndPoint0.PipeId,
            sizeof(USBFNPIPEID),
            writeBuf,
            writeBufLen,
            ContinuousRequestTarget_RequestType_InternalIoctl,
            writeBufLen % m_EndPoint0.EpDesc.wMaxPacketSize ?
                IOCTL_INTERNAL_USBFN_TRANSFER_IN :
                IOCTL_INTERNAL_USBFN_TRANSFER_IN_APPEND_ZERO_PKT,
            0,
            nullptr);

    TraceInfo(USBNCM_FUNCTION, "Write to ep0 completed, %!status!", status);

    if (NT_SUCCESS(status))
    {
        SendHandshake();
    }

    return status;
}

PAGEDX
_Use_decl_annotations_
VOID
UsbNcmFunctionDevice::StallEndPoint0()
{
    // Stall pipe 0, as part of the status stage
    WDFREQUEST request = nullptr;

    if (NT_SUCCESS(UsbFnKmClassLibCreatePipeStateSetRequest(m_UsbFnClassLibHandle,
                                                            m_EndPoint0.PipeId,
                                                            TRUE,
                                                            nullptr,
                                                            &request)))
    {
        if (!WdfRequestSend(request, WdfDeviceGetIoTarget(m_WdfDevice), WDF_NO_SEND_OPTIONS))
        {
            WdfObjectDelete(request);
        }
    }
}

PAGEDX
_Use_decl_annotations_
VOID
UsbNcmFunctionDevice::ProcessSetupPacket(
    const USB_DEFAULT_PIPE_SETUP_PACKET& SetupPacket)
{
    NcmRequestType requestType = static_cast<NcmRequestType>(SetupPacket.bRequest);
    bool stallEp0 = true;

    NT_FRE_ASSERT(m_State == UsbfnDeviceStateConfigured);

    //
    // if function side doesn't support this, stall
    // if this is invalid input, stall
    // else do data stage to read/write Ep0, follow by handshake in opposite direction,
    // if no data stage, just handshake_out to inidicate finish.
    //
    // For now, since this function driver doesn't advertise anything else, we should except only
    // required Setup packets to show up, so we stall by default.
    //

    TraceInfo(USBNCM_FUNCTION, "Received setup packet request: type %d", requestType);

    if (SetupPacket.wIndex.W == m_CommunicationInterfaceIndex)
    {
        switch (requestType)
        {
            case NcmReqGetNetAddr: // optional
            case NcmReqSetNetAddr: // optional
            case NcmReqGetMaxDatagramSize: // optional
            case NcmReqSetMaxDatagramSize: // optional
            case NcmReqGetCrcMode: // optional
            case NcmReqSetCrcMode: // optional
            case NcmReqSetEthMcastFilts: // optional
            case NcmReqSetEthPwrMgmtFilt: // optional
            case NcmReqGetEthPwrMgmtFilt: // optional
            case NcmReqSetEthPacketFilt: // optional
            case NcmReqGetEthStat: // optional
            {
                TraceInfo(USBNCM_FUNCTION, "Unsupported setup packet request: type %d", requestType);
                break;
            }

            case NcmReqGetNtbParams: // required
            {
                if ((SetupPacket.wValue.W != 0) || (SetupPacket.wLength < sizeof(NTB_PARAMETERS)))
                {
                    break;
                }

                if (NT_SUCCESS(WriteToEndPoint0((PVOID) &m_NtbParamters, sizeof(NTB_PARAMETERS))))
                {
                    stallEp0 = false;
                }

                break;
            }

            case NcmReqGetNtbFmt: // required as we support 32 bit NTBs
            {
                if ((SetupPacket.wValue.W != 0) || (SetupPacket.wLength < 2))
                {
                    break;
                }

                UINT16 ntbFormat = m_Use32BitNtb ? 1 : 0;

                if (NT_SUCCESS(WriteToEndPoint0((PVOID) &ntbFormat, sizeof(UINT16))))
                {
                    stallEp0 = false;
                }

                break;
            }

            case NcmReqSetNtbFmt: // required since we support 32 bit NTBs
            {
                if ((SetupPacket.wValue.W > 1) ||
                    (SetupPacket.wLength != 0) ||
                    (m_AlternateInterfaceNumber != 0))
                {
                    break;
                }

                m_Use32BitNtb = (SetupPacket.wValue.W == 1);

                if (NT_SUCCESS(SendHandshake()))
                {
                    stallEp0 = false;
                }

                break;
            }

            case NcmReqGetNtbInputSize: // required
            {
                if ((SetupPacket.wValue.W != 0) || (SetupPacket.wLength < 4))
                {
                    break;
                }

                if (NT_SUCCESS(WriteToEndPoint0((PVOID) &m_HostSelectedNtbInMaxSize,
                                                sizeof(UINT32))))
                {
                    stallEp0 = false;
                }

                break;
            }

            case NcmReqSetNtbInputSize: // required
            {
                //
                // This changes the max size of a send function side can issue to the host
                //
                if ((SetupPacket.wValue.W != 0) || (SetupPacket.wLength != 4))
                {
                    break;
                }

                UINT32 ntbInMaxSize = 0;

                if (NT_SUCCESS(ReadFromEndPoint0(sizeof(UINT32), (PVOID) &ntbInMaxSize)))
                {
                    if ((ntbInMaxSize >= 2048) &&
                        (ntbInMaxSize <= m_NtbParamters.dwNtbInMaxSize))
                    {
                        m_HostSelectedNtbInMaxSize = ntbInMaxSize;

                        if (m_NetAdapter != nullptr)
                        {
                            // we need to reallocate buffer size since the max input
                            // size may have changed.
                            DestroyAdapter();
                            (void) CreateAdapter();
                        }
                    }

                    stallEp0 = false;
                }

                break;
            }

            default:
            {
                break;
            }
        }
    }

    if (stallEp0)
    {
        //
        // we need to fail this setup packet by stalling ep0
        //
        StallEndPoint0();
    }
}

#pragma endregion

#pragma region UsbNcm Fn Bus Notification

//
// UsbNcm Fn Bus Notification
//

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::SubscribeBusEventNotification()
{
    DMF_ContinuousRequestTarget_IoTargetSet(m_ControlContinuousRequest,
                                            WdfDeviceGetIoTarget(m_WdfDevice));

    return DMF_ContinuousRequestTarget_Start(m_ControlContinuousRequest);
}

PAGEDX
_Use_decl_annotations_
VOID
UsbNcmFunctionDevice::UnSubscribeBusEventNotification()
{
    DMF_ContinuousRequestTarget_StopAndWait(m_ControlContinuousRequest);
    DMF_ContinuousRequestTarget_IoTargetClear(m_ControlContinuousRequest);
}

PAGEDX
_Use_decl_annotations_
ContinuousRequestTarget_BufferDisposition
UsbNcmFunctionDevice::BusEventNotificationRead(
    DMFMODULE dmfModule,
    VOID* outputBuffer,
    size_t,
    VOID*,
    NTSTATUS completionStatus)
{
    UsbNcmFunctionDevice* functionDevice =
        NcmGetFunctionDeviceFromHandle(DMF_ParentDeviceGet(dmfModule));

    // Get status of the request.  If we fail here, the device is likely
    // in the process of leaving the D0 power state

    if (NT_SUCCESS(completionStatus))
    {
        PUSBFN_NOTIFICATION notification = (PUSBFN_NOTIFICATION) outputBuffer;

        NT_FRE_ASSERT(notification != nullptr);

        //
        // The order of notification we expect to see (in this order)
        // 1) Attach (no-op, update bus speed)
        // 2) Configured (update function adapter link speed, link state, send network connect and network speed change to host)
        //        -> After this point phone OS may attemp to send data, but we don't send it as data interface has not been enabled yet.
        //           But at the same time, we know host is not going to send data since it hasn't switch data interface yet. At this point,
        //           the receive IO shouldn't be queued and no send IO should be issued.
        //        -> We may also get reset events prior to configured event, in that case, resets should be ignored.
        // 3) SetupPacket, we should be able to handle all setup packets when data interface is on setting 0, and some when data interface is on
        //    Setting 1. At this point, still no data IO yet. These request should only be seen after configured event, this may show up anytime.
        // 4) SetInterface, this will switch data interface setting, kick off receive data IO requests on the pipe, permit send data, also notify
        //    CX send notification. Receive notification will be sent to CX once we get IO complete from receive data IO requests.
        // 5) Reset, update bus speed, indicate link speed to function adapter & send notification of speed and connection to host, cancel & requeue receive IO
        //    and also attempt to cancel on going send IO, if any. notify cx for send and restart send from Next pointer, 
        //    succesful cancel shouldn't update next pointer.
        // 6) Unconfigure/Detach, update bus speed, indicate link speed & send notification of speed and connection to host, cancel receive IO, send IO
        //    and notification IO.
        // 7) suspend/resume, not handling power for now.
        //

        TraceInfo(USBNCM_FUNCTION, "Received Bus Event Notification %d", notification->Event);

        UsbFnKmClassLibHelperPreProcessBusEventSubscription(
            notification,
            (KUSBFNCLASSLIB_CONTEXT*) functionDevice->m_UsbFnClassLibHandle);

        switch (notification->Event)
        {
            case UsbfnEventAttach:
            {
                functionDevice->m_BusSpeed = notification->u.BusSpeed;
                functionDevice->m_State = UsbfnDeviceStateAttached;
                break;
            }
            case UsbfnEventSuspend:
            case UsbfnEventResume:
            {
                break;
            }
            case UsbfnEventConfigured:
            {
                functionDevice->m_State = UsbfnDeviceStateConfigured;
                break;
            }
            case UsbfnEventUnConfigured:
            {
                functionDevice->m_State = UsbfnDeviceStateAttached;
                functionDevice->DestroyAdapter();
                break;
            }
            case UsbfnEventDetach:
            {
                functionDevice->m_State = UsbfnDeviceStateDetached;
                functionDevice->DestroyAdapter();
                break;
            }
            case UsbfnEventReset:
            {
                functionDevice->m_State = UsbfnDeviceStateDefault;
                functionDevice->m_BusSpeed = notification->u.BusSpeed;
                functionDevice->DestroyAdapter();
                break;
            }
            case UsbfnEventSetupPacket:
            {
                functionDevice->ProcessSetupPacket(notification->u.SetupPacket);
                break;
            }
            case UsbfnEventSetInterface:
            {
                NT_FRE_ASSERT(functionDevice->m_State == UsbfnDeviceStateConfigured);

                ALTERNATE_INTERFACE alternateInterface = notification->u.AlternateInterface;

                TraceInfo(USBNCM_FUNCTION, "Interface %u select alternate number %u",
                          alternateInterface.InterfaceNumber, alternateInterface.AlternateInterfaceNumber);

                if (functionDevice->m_AlternateInterfaceNumber != alternateInterface.AlternateInterfaceNumber)
                {
                    functionDevice->m_AlternateInterfaceNumber = alternateInterface.AlternateInterfaceNumber;

                    if (alternateInterface.AlternateInterfaceNumber == 0)
                    {
                        functionDevice->DestroyAdapter();
                    }
                    else if (alternateInterface.AlternateInterfaceNumber == 1)
                    {
                        (void) functionDevice->CreateAdapter();
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    else
    {
        TraceInfo(USBNCM_FUNCTION, "Failed to receive Bus Event Notification. Status = %!status!", completionStatus);
    }

    return ContinuousRequestTarget_BufferDisposition_ContinuousRequestTargetAndContinueStreaming;
}

#pragma endregion

#pragma region UsbNcm Fn Bulkout

//
// UsbNcm Fn Bulkout
//

PAGEDX
_Use_decl_annotations_
void
UsbNcmFunctionDevice::GetPipeMemoryForDataReader(
    DMFMODULE dmfModule,
    VOID* inputBuffer,
    size_t* inputBufferSize,
    VOID*)
{
    UsbNcmFunctionDevice* functionDevice =
        NcmGetFunctionDeviceFromHandle(DMF_ParentDeviceGet(dmfModule));

    USBFNPIPEID* pipeId = (USBFNPIPEID*) inputBuffer;

    *pipeId = functionDevice->m_BulkOut.PipeId;

    *inputBufferSize = sizeof(USBFNPIPEID);
}

PAGEDX
_Use_decl_annotations_
ContinuousRequestTarget_BufferDisposition
UsbNcmFunctionDevice::DataBulkOutPipeRead(
    DMFMODULE dmfModule,
    VOID* outputBuffer,
    size_t outputBufferSize,
    VOID*,
    NTSTATUS completionStatus)
{
    if (NT_SUCCESS(completionStatus))
    {
        UsbNcmFunctionDevice* functionDevice =
            NcmGetFunctionDeviceFromHandle(DMF_ParentDeviceGet(dmfModule));

        functionDevice->m_NcmAdapterCallbacks->EvtUsbNcmAdapterNotifyReceive(
            functionDevice->m_NetAdapter,
            (PUCHAR) outputBuffer,
            outputBufferSize,
            WDF_NO_HANDLE,
            functionDevice->m_DataContinuousRequest);

        return ContinuousRequestTarget_BufferDisposition_ClientAndContinueStreaming;
    }
    else
    {
        TraceInfo(USBNCM_FUNCTION, "DataBulkOutPipeRead: Status = %!status!", completionStatus);
        return ContinuousRequestTarget_BufferDisposition_ContinuousRequestTargetAndContinueStreaming;
    }
}

#pragma endregion

#pragma region UsbNcm Fn NetAdapter

//
// UsbNcm Fn NetAdapter
//

// TODO Bus notfication callback must be invoked at passive

PAGEDX
_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::CreateAdapter()
{
    NT_FRE_ASSERT(m_NetAdapter == nullptr);

    NCM_RETURN_IF_NOT_NT_SUCCESS(CacheClassInformation());

    BYTE adapterMacAddress[ETH_LENGTH_OF_ADDRESS] = {};

    RtlCopyMemory(adapterMacAddress, m_CdcMacAddress, ETH_LENGTH_OF_ADDRESS);
    GetNextMACAddress(adapterMacAddress);

    USBNCM_ADAPTER_PARAMETERS parameters = {
        m_Use32BitNtb,
        adapterMacAddress,
        9014, // must match the value specified in EEM function descriptor in UsbNcmFn.inf
        16,
        m_HostSelectedNtbInMaxSize,
        m_NtbParamters.wNdpInAlignment,
        m_NtbParamters.wNdpInDivisor,
        m_NtbParamters.wNdpInPayloadRemainder,
    };

    NCM_RETURN_IF_NOT_NT_SUCCESS(
        UsbNcmAdapterCreate(m_WdfDevice,
                            &parameters,
                            &UsbNcmFunctionDevice::s_NcmDeviceCallbacks,
                            &m_NetAdapter,
                            &m_NcmAdapterCallbacks));

    m_NcmAdapterCallbacks->EvtUsbNcmAdapterSetLinkSpeed(m_NetAdapter,
                                                        g_UsbFnBusSpeed[m_BusSpeed],
                                                        g_UsbFnBusSpeed[m_BusSpeed]);

    m_NcmAdapterCallbacks->EvtUsbNcmAdapterSetLinkState(m_NetAdapter, true);

    NotifyConnectionSpeedAndStatusChange(true,
                                         g_UsbFnBusSpeed[m_BusSpeed]);

    TraceInfo(USBNCM_FUNCTION, "Create Adapter succeeded");

    return STATUS_SUCCESS;
}

PAGEDX
_Use_decl_annotations_
VOID
UsbNcmFunctionDevice::DestroyAdapter()
{
    if (m_NetAdapter)
    {
        m_NcmAdapterCallbacks->EvtUsbNcmAdapterSetLinkSpeed(m_NetAdapter,
                                                            0,
                                                            0);

        m_NcmAdapterCallbacks->EvtUsbNcmAdapterSetLinkState(m_NetAdapter, false);

        UsbNcmAdapterDestory(m_NetAdapter);

        m_NcmAdapterCallbacks = nullptr;
        m_NetAdapter = nullptr;

        SetParameters();
        CacheClassInformation();
        NotifyConnectionSpeedAndStatusChange(false, 0);

        m_AlternateInterfaceNumber = 0;
    }
}

#pragma endregion

#pragma region UsbNcm Fn Device Interface

//
// UsbNcm Fn Device Interface
//

PAGEDX
_Use_decl_annotations_
void
UsbNcmFunctionDevice::StartReceive(
    WDFDEVICE usbNcmWdfDevice)
{
    TraceInfo(USBNCM_FUNCTION, "Start Receive");

    UsbNcmFunctionDevice* functionDevice = NcmGetFunctionDeviceFromHandle(usbNcmWdfDevice);

    DMF_ContinuousRequestTarget_IoTargetSet(
        functionDevice->m_DataContinuousRequest, 
        WdfDeviceGetIoTarget(usbNcmWdfDevice));

    (void) DMF_ContinuousRequestTarget_Start(functionDevice->m_DataContinuousRequest);
}

PAGEDX
_Use_decl_annotations_
void
UsbNcmFunctionDevice::StopReceive(
    WDFDEVICE usbNcmWdfDevice)
{
    TraceInfo(USBNCM_FUNCTION, "Stop Receive");

    UsbNcmFunctionDevice* functionDevice = NcmGetFunctionDeviceFromHandle(usbNcmWdfDevice);

    DMF_ContinuousRequestTarget_StopAndWait(functionDevice->m_DataContinuousRequest);
    DMF_ContinuousRequestTarget_IoTargetClear(functionDevice->m_DataContinuousRequest);
}

PAGEDX
_Use_decl_annotations_
void
UsbNcmFunctionDevice::StartTransmit(
    WDFDEVICE usbNcmWdfDevice)
{
    TraceInfo(USBNCM_FUNCTION, "Start Transmit");

    UsbNcmFunctionDevice* functionDevice = NcmGetFunctionDeviceFromHandle(usbNcmWdfDevice);

    ExReInitializeRundownProtection(&functionDevice->m_BulkInRundown);
}

PAGEDX
_Use_decl_annotations_
void
UsbNcmFunctionDevice::StopTransmit(
    WDFDEVICE usbNcmWdfDevice)
{
    TraceInfo(USBNCM_FUNCTION, "Stop Transmit");

    UsbNcmFunctionDevice* functionDevice = NcmGetFunctionDeviceFromHandle(usbNcmWdfDevice);

    ExWaitForRundownProtectionRelease(&functionDevice->m_BulkInRundown);
}

NTSTATUS
FormatRequestForUsbFnTransferIn(
    __in KUSBFNCLASSLIBHANDLE classHandle,
    __in WDFIOTARGET target,
    __in USBFNPIPEID pipeId,
    __in WDFREQUEST request,
    __in WDFMEMORY dataMemory,
    __in WDFMEMORY_OFFSET* offset,
    __in BOOLEAN appendZlp)
{
    WDFMEMORY pipeIdMemory;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        UsbFnKmClassLibPipeIdWdfMemoryFind(classHandle,
                                           pipeId,
                                           &pipeIdMemory),
        "UsbFnKmClassLibPipeIdWdfMemoryFind failed");

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfIoTargetFormatRequestForInternalIoctl(target,
                                                 request,
                                                 appendZlp ? IOCTL_INTERNAL_USBFN_TRANSFER_IN_APPEND_ZERO_PKT
                                                           : IOCTL_INTERNAL_USBFN_TRANSFER_IN,
                                                 pipeIdMemory,
                                                 0,
                                                 dataMemory,
                                                 offset),
        "WdfIoTargetFormatRequestForInternalIoctl failed");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
inline
void
UsbNcmFunctionDevice::TransmitFramesCompetion(
    WDFREQUEST,
    WDFIOTARGET target,
    PWDF_REQUEST_COMPLETION_PARAMS,
    WDFCONTEXT context)
{
    UsbNcmFunctionDevice* functionDevice =
        NcmGetFunctionDeviceFromHandle(WdfIoTargetGetDevice(target));

    functionDevice->m_NcmAdapterCallbacks->EvtUsbNcmAdapterNotifyTransmitCompletion(
        functionDevice->m_NetAdapter,
        (TX_BUFFER_REQUEST*) context);

    ExReleaseRundownProtection(&functionDevice->m_BulkInRundown);
}

_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionDevice::TransmitFrames(
    WDFDEVICE usbNcmWdfDevice,
    TX_BUFFER_REQUEST* bufferRequest)
{
    NTSTATUS status = STATUS_SUCCESS;
    UsbNcmFunctionDevice* functionDevice = NcmGetFunctionDeviceFromHandle(usbNcmWdfDevice);

    if (ExAcquireRundownProtection(&functionDevice->m_BulkInRundown) != FALSE)
    {
        NT_FRE_ASSERT(bufferRequest->TransferLength > 0);

        WdfRequestSetCompletionRoutine(bufferRequest->Request,
                                       UsbNcmFunctionDevice::TransmitFramesCompetion,
                                       bufferRequest);

        WDFMEMORY_OFFSET offset{ 0, bufferRequest->TransferLength };

        status =
            FormatRequestForUsbFnTransferIn(
                functionDevice->m_UsbFnClassLibHandle,
                WdfDeviceGetIoTarget(functionDevice->m_WdfDevice),
                functionDevice->m_BulkIn.PipeId,
                bufferRequest->Request,
                bufferRequest->BufferWdfMemory,
                &offset,
                0 == (bufferRequest->TransferLength % functionDevice->m_BulkIn.EpDesc.wMaxPacketSize));

        if (NT_SUCCESS(status))
        {
            WDF_REQUEST_SEND_OPTIONS sendOptions = {};
            WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
            WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions, WDF_REL_TIMEOUT_IN_SEC(3));

            if (!WdfRequestSend(bufferRequest->Request,
                                WdfDeviceGetIoTarget(functionDevice->m_WdfDevice), &sendOptions))
            {
                status = WdfRequestGetStatus(bufferRequest->Request);
            }
        }

        if (!NT_SUCCESS(status))
        {
            ExReleaseRundownProtection(&functionDevice->m_BulkInRundown);
        }
    }
    else
    {
        status = STATUS_CANCELLED;
    }

    return status;
}

#pragma endregion
