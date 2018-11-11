// Copyright (C) Microsoft Corporation. All rights reserved.

#include "driver.h"
#include "device.tmh"

#define MAX_HOST_NTB_SIZE               (0x10000)
#define MAX_HOST_MTU_SIZE               (9014)
#define MAX_HOST_TX_NTB_DATAGRAM_COUNT  (UINT16) (16)
#define PENDING_BULK_IN_READS           (3)

const USBNCM_DEVICE_EVENT_CALLBACKS UsbNcmHostDevice::s_NcmDeviceCallbacks = {
    sizeof(USBNCM_DEVICE_EVENT_CALLBACKS),
    UsbNcmHostDevice::StartReceive,
    UsbNcmHostDevice::StopReceive,
    UsbNcmHostDevice::StartTransmit,
    UsbNcmHostDevice::StopTransmit,
    UsbNcmHostDevice::TransmitFrames
};

_IRQL_requires_max_(DISPATCH_LEVEL)
void StartPipe(_In_ WDFUSBPIPE pipe)
{
    WDFIOTARGET wdfIotarget;

    wdfIotarget = WdfUsbTargetPipeGetIoTarget(pipe);
    (void) WdfIoTargetStart(wdfIotarget);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void StopPipe(_In_ WDFUSBPIPE pipe)
{
    WDFIOTARGET wdfIotarget;

    wdfIotarget = WdfUsbTargetPipeGetIoTarget(pipe);
    WdfIoTargetStop(wdfIotarget, WdfIoTargetCancelSentIo);
}

PAGED
_Use_decl_annotations_
NTSTATUS 
UsbNcmHostDevice::InitializeDevice()
{
    WDF_USB_DEVICE_INFORMATION deviceInfo;
    WDF_USB_DEVICE_CREATE_CONFIG createParams;

    PAGED_CODE();

    WDF_USB_DEVICE_CREATE_CONFIG_INIT(&createParams,
                                        USBD_CLIENT_CONTRACT_VERSION_602);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbTargetDeviceCreateWithParameters(m_WdfDevice,
                                               &createParams,
                                               WDF_NO_OBJECT_ATTRIBUTES,
                                               &m_WdfUsbTargetDevice),
        "WdfUsbTargetDeviceCreateWithParameters failed");

    //
    // Retrieve USBD version information, port driver capabilites and device
    // capabilites such as speed, power, etc.
    //

    WDF_USB_DEVICE_INFORMATION_INIT(&deviceInfo);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbTargetDeviceRetrieveInformation(
            m_WdfUsbTargetDevice,
            &deviceInfo),
        "WdfUsbTargetDeviceRetrieveInformation failed");

    NCM_RETURN_IF_NOT_NT_SUCCESS(RetrieveConfiguration());

    NCM_RETURN_IF_NOT_NT_SUCCESS(SelectConfiguration());

    WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;

    //  RX Path: Configure the continous reader on the bulk pipe now, since this can 
    //  only be done once on a given pipe unless it is unselected
    WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&readerConfig,
                                          UsbNcmHostDevice::DataBulkInPipeReadCompletetionRoutine,
                                          this,
                                          m_HostSelectedNtbInMaxSize);

    readerConfig.HeaderLength = 0;
    readerConfig.NumPendingReads = PENDING_BULK_IN_READS;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbTargetPipeConfigContinuousReader(m_DataBulkInPipe,
                                               &readerConfig),
        "WdfUsbTargetPipeConfigContinuousReader failed for bulkin pipe");

    //
    // Configure the continous reader for Interrupt pipe
    //
    WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&readerConfig,
                                          UsbNcmHostDevice::ControlInterruptPipeReadCompletetionRoutine,
                                          this,
                                          m_ControlInterruptPipeMaxPacket);
    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbTargetPipeConfigContinuousReader(m_ControlInterruptPipe,
                                               &readerConfig),
        "WdfUsbTargetPipeConfigContinuousReader failed for interrupt pipe");

    return STATUS_SUCCESS;
}

PAGED
_Use_decl_annotations_
NTSTATUS
UsbNcmHostDevice::CreateAdapter()
{
    PAGED_CODE();

    USBNCM_ADAPTER_PARAMETERS parameters = {
        m_Use32BitNtb,
        m_MacAddress,
        m_MaxDatagramSize,
        m_NtbParamters.wNtbOutMaxDatagrams > 0 ?
            m_NtbParamters.wNtbOutMaxDatagrams :
            MAX_HOST_TX_NTB_DATAGRAM_COUNT,
        m_NtbParamters.dwNtbOutMaxSize,
        m_NtbParamters.wNdpOutAlignment,
        m_NtbParamters.wNdpOutDivisor,
        m_NtbParamters.wNdpOutPayloadRemainder,
    };

    NCM_RETURN_IF_NOT_NT_SUCCESS(
        UsbNcmAdapterCreate(m_WdfDevice,
                            &parameters,
                            &UsbNcmHostDevice::s_NcmDeviceCallbacks,
                            &m_NetAdapter,
                            &m_NcmAdapterCallbacks));

    StartPipe(m_ControlInterruptPipe);

    return STATUS_SUCCESS;
}

PAGED
_Use_decl_annotations_
void
UsbNcmHostDevice::DestroyAdapter()
{
    PAGED_CODE();

    if (m_NetAdapter != nullptr)
    {
        UsbNcmAdapterDestory(m_NetAdapter);
        StopPipe(m_ControlInterruptPipe);
    }
}

PAGED
_Use_decl_annotations_
NTSTATUS
UsbNcmHostDevice::RequestClassSpecificControlTransfer(
    UINT8 request,
    WDF_USB_BMREQUEST_DIRECTION direction,
    WDF_USB_BMREQUEST_RECIPIENT recipient,
    UINT16 value,
    PWDF_MEMORY_DESCRIPTOR memoryDescriptor)
{
    WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
    WDF_REQUEST_SEND_OPTIONS sendOptions;

    PAGED_CODE();

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(&controlSetupPacket,
                                            direction,
                                            recipient,
                                            request,
                                            value,
                                            WdfUsbInterfaceGetInterfaceNumber(m_ControlInterface));

    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, 0);
    //WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions, WDF_REL_TIMEOUT_IN_SEC(10));

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbTargetDeviceSendControlTransferSynchronously(m_WdfUsbTargetDevice,
                                                           WDF_NO_HANDLE,
                                                           &sendOptions,
                                                           &controlSetupPacket,
                                                           memoryDescriptor,
                                                           nullptr),
        "WdfUsbTargetDeviceSendControlTransferSynchronously failed");

    return STATUS_SUCCESS;
}

PAGED
_Use_decl_annotations_
NTSTATUS
UsbNcmHostDevice::RetrieveConfiguration()
{
    WDF_OBJECT_ATTRIBUTES objectAttribs;

    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;

    //
    // 1. Retrieve all descriptors for this NCM USB device
    //

    PUSB_CONFIGURATION_DESCRIPTOR pDescriptors = NULL;
    USHORT sizeDescriptors;
    WDFMEMORY descriptorMemory;

    status = WdfUsbTargetDeviceRetrieveConfigDescriptor(m_WdfUsbTargetDevice,
                                                        NULL,
                                                        &sizeDescriptors);
    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(status == STATUS_BUFFER_TOO_SMALL, status,
                                      "WdfUsbTargetDeviceRetrieveConfigDescriptor failed");

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttribs);
    objectAttribs.ParentObject = m_WdfDevice;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfMemoryCreate(&objectAttribs,
                        NonPagedPoolNx,
                        0,
                        sizeDescriptors,
                        &descriptorMemory,
                        (PVOID*) &pDescriptors),
        "WdfMemoryCreate failed");

    RtlZeroMemory(pDescriptors, sizeDescriptors);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbTargetDeviceRetrieveConfigDescriptor(m_WdfUsbTargetDevice,
                                                    pDescriptors,
                                                    &sizeDescriptors),
        "WdfUsbTargetDeviceRetrieveConfigDescriptor failed");

    //
    // 2. scan the descriptors for communication and data interfaces
    //

    BYTE controlInterfaceNumber = 0;
    BYTE dataInterfaceNumber = 0;
    PUSB_NCM_CS_FUNCTIONAL_DESCRIPTOR pNcmFunctionalDescr = nullptr;
    PUSB_ECM_CS_NET_FUNCTIONAL_DESCRIPTOR pEcmFunctionalDescr = nullptr;

    size_t currDescrptorOffset = 0;
    PUSB_COMMON_DESCRIPTOR pCurrDescriptor = (PUSB_COMMON_DESCRIPTOR) pDescriptors;

    while (currDescrptorOffset < pDescriptors->wTotalLength)
    {
        switch (pCurrDescriptor->bDescriptorType)
        {
            case USB_INTERFACE_DESCRIPTOR_TYPE:
            {
                NCM_RETURN_NT_STATUS_IF_FALSE_MSG(pCurrDescriptor->bLength == sizeof(USB_INTERFACE_DESCRIPTOR),
                                                  STATUS_DEVICE_HARDWARE_ERROR,
                                                  "Bad UsbInterfaceDescriptor");

                PUSB_INTERFACE_DESCRIPTOR pIfDescriptor = (PUSB_INTERFACE_DESCRIPTOR) pCurrDescriptor;

                if ((pIfDescriptor->bInterfaceClass == USB_CDC_INTERFACE_CLASS_COMM) &&
                    (pIfDescriptor->bInterfaceSubClass == USB_CDC_INTERFACE_SUBCLASS_NCM))
                {
                    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(pIfDescriptor->bNumEndpoints == 1,
                                                      STATUS_DEVICE_HARDWARE_ERROR,
                                                      "Bad UsbInterfaceDescriptor");

                    controlInterfaceNumber = pIfDescriptor->bInterfaceNumber;
                }
                else if ((pIfDescriptor->bInterfaceClass == USB_CDC_INTERFACE_CLASS_DATA) &&
                         (pIfDescriptor->bInterfaceProtocol == USB_DATA_INTERFACE_PROTOCOL_NCM) &&
                         (pIfDescriptor->bAlternateSetting == 1))
                {
                    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(pIfDescriptor->bNumEndpoints == 2,
                                                      STATUS_DEVICE_HARDWARE_ERROR,
                                                      "Bad UsbInterfaceDescriptor");

                    dataInterfaceNumber = pIfDescriptor->bInterfaceNumber;
                }

                break;
            }

            case USB_CS_INTERFACE_TYPE:
            {
                NCM_RETURN_NT_STATUS_IF_FALSE_MSG(pCurrDescriptor->bLength >= sizeof(USB_CDC_CS_FUNCTIONAL_DESCRIPTOR),
                                                  STATUS_DEVICE_HARDWARE_ERROR,
                                                  "Bad UsbCdcFunctionalDescriptor");

                PUSB_CDC_CS_FUNCTIONAL_DESCRIPTOR pCsFuncDescriptor =
                    (PUSB_CDC_CS_FUNCTIONAL_DESCRIPTOR) pCurrDescriptor;

                switch (pCsFuncDescriptor->bDescriptorSubtype)
                {
                    // NCM functional descriptor 
                    case USB_CS_NCM_FUNCTIONAL_DESCR_TYPE:
                    {
                        NCM_RETURN_NT_STATUS_IF_FALSE_MSG(
                            pCsFuncDescriptor->bFunctionLength == sizeof(USB_NCM_CS_FUNCTIONAL_DESCRIPTOR),
                            STATUS_DEVICE_HARDWARE_ERROR,
                            "Bad UsbNcmFunctionalDescriptor");

                        pNcmFunctionalDescr = (PUSB_NCM_CS_FUNCTIONAL_DESCRIPTOR) pCsFuncDescriptor;

                        break;
                    }

                    // ECM network functional descriptor
                    case USB_CS_ECM_FUNCTIONAL_DESCR_TYPE:
                    {
                        NCM_RETURN_NT_STATUS_IF_FALSE_MSG(
                            pCsFuncDescriptor->bFunctionLength == sizeof(USB_ECM_CS_NET_FUNCTIONAL_DESCRIPTOR),
                            STATUS_DEVICE_HARDWARE_ERROR,
                            "Bad UsbEcmFunctionalDescriptor");

                        pEcmFunctionalDescr = (PUSB_ECM_CS_NET_FUNCTIONAL_DESCRIPTOR) pCsFuncDescriptor;

                        break;
                    }
                }

                break;
            }
        }

        currDescrptorOffset += pCurrDescriptor->bLength;
        pCurrDescriptor = (PUSB_COMMON_DESCRIPTOR)(((PUINT8)pCurrDescriptor) + pCurrDescriptor->bLength);
    }

    BYTE numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(m_WdfUsbTargetDevice);

    for (UCHAR ifIndex = 0; ifIndex < numInterfaces; ifIndex++)
    {
        WDFUSBINTERFACE usbInterface = WdfUsbTargetDeviceGetInterface(m_WdfUsbTargetDevice,
                                                                      ifIndex);

        if (WdfUsbInterfaceGetInterfaceNumber(usbInterface) == controlInterfaceNumber)
        {
            m_ControlInterface = usbInterface;
        }
        else if (WdfUsbInterfaceGetInterfaceNumber(usbInterface) == dataInterfaceNumber)
        {
            m_DataInterface = usbInterface;
        }
    }

    NCM_RETURN_NT_STATUS_IF_FALSE_MSG((m_ControlInterface != nullptr) && (m_DataInterface != nullptr),
                                      STATUS_DEVICE_HARDWARE_ERROR,
                                      "Bad UsbNcm interfaces");

    // reset both interfaces back to default
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
    WDF_USB_INTERFACE_SETTING_PAIR settingPair[2] = { { m_ControlInterface, 0}, { m_DataInterface , 0} };

    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(&configParams,
                                                                 ARRAYSIZE(settingPair),
                                                                 settingPair);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(WdfUsbTargetDeviceSelectConfig(m_WdfUsbTargetDevice,
                                                                    WDF_NO_OBJECT_ATTRIBUTES,
                                                                    &configParams),
                                     "WdfUsbTargetDeviceSelectConfig failed");

    NCM_RETURN_IF_NOT_NT_SUCCESS(
        ConfigCapabilities(pNcmFunctionalDescr,
                           pEcmFunctionalDescr));

    return STATUS_SUCCESS;
}

PAGED
_Use_decl_annotations_
NTSTATUS
UsbNcmHostDevice::ConfigCapabilities(PUSB_NCM_CS_FUNCTIONAL_DESCRIPTOR pNcmFunctionalDescr,
                                     PUSB_ECM_CS_NET_FUNCTIONAL_DESCRIPTOR pEcmFunctionalDescr)
{
    WCHAR strMacAddress[12];
    USHORT strMacAddressLength = ARRAYSIZE(strMacAddress);

    PAGED_CODE();

    UNREFERENCED_PARAMETER(pNcmFunctionalDescr);

    m_MaxDatagramSize = min(pEcmFunctionalDescr->wMaxSegmentSize, MAX_HOST_MTU_SIZE);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbTargetDeviceQueryString(m_WdfUsbTargetDevice,
                                      NULL,
                                      NULL,
                                      strMacAddress,
                                      &strMacAddressLength,
                                      pEcmFunctionalDescr->iMACAddress,
                                      0x0409),
        "WdfUsbTargetDeviceQueryString failed");

    HexStringToBytes(strMacAddress, m_MacAddress, sizeof(m_MacAddress));

    WDF_MEMORY_DESCRIPTOR memoryDescriptor;

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor,
                                      &m_NtbParamters,
                                      sizeof(m_NtbParamters));

    NCM_RETURN_IF_NOT_NT_SUCCESS(
        RequestClassSpecificControlTransfer(
            USB_REQUEST_GET_NTB_PARAMETERS,
            BmRequestDeviceToHost,
            BmRequestToInterface,
            0,
            &memoryDescriptor));

    // NTB 16 must be supported
    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(m_NtbParamters.bmNtbFormatsSupported & 0x1,
                                      STATUS_DEVICE_HARDWARE_ERROR,
                                      "Bad NTB format");

    //using NTB 32 if supported
    if (m_NtbParamters.bmNtbFormatsSupported & 0x2)
    {

        //6.2.5
        //The host shall only send this command while the NCM Data Interface is in alternate setting 0.

        NCM_RETURN_IF_NOT_NT_SUCCESS(
            RequestClassSpecificControlTransfer(USB_REQUEST_SET_NTB_FORMAT,
                                                BmRequestHostToDevice,
                                                BmRequestToInterface,
                                                1,
                                                nullptr));

        m_Use32BitNtb = TRUE;
    }

    m_HostSelectedNtbInMaxSize = min(m_NtbParamters.dwNtbInMaxSize,
                                     MAX_HOST_NTB_SIZE);

    // 3.4 NTB Maximum Sizes
    // 6.2.7 SetNtbInputSize 
    if (m_NtbParamters.dwNtbInMaxSize > MAX_HOST_NTB_SIZE)
    {
        m_HostSelectedNtbInMaxSize = MAX_HOST_NTB_SIZE;

        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
            &memoryDescriptor,
            (PVOID) &m_HostSelectedNtbInMaxSize,
            sizeof(m_HostSelectedNtbInMaxSize));

        NCM_RETURN_IF_NOT_NT_SUCCESS(
            RequestClassSpecificControlTransfer(
                USB_REQUEST_SET_NTB_INPUT_SIZE,
                BmRequestHostToDevice,
                BmRequestToInterface,
                0,
                &memoryDescriptor));
    }
    else
    {
        m_HostSelectedNtbInMaxSize = m_NtbParamters.dwNtbInMaxSize;
    }

    return STATUS_SUCCESS;

}

PAGED
_Use_decl_annotations_
NTSTATUS
UsbNcmHostDevice::SelectConfiguration()
{
    WDF_USB_INTERFACE_SELECT_SETTING_PARAMS settingParams;

    PAGED_CODE();

    // control interface remains at Setting 0, data interface is selected to Setting 1
    WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING(&settingParams, 1);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfUsbInterfaceSelectSetting(m_DataInterface, WDF_NO_OBJECT_ATTRIBUTES, &settingParams),
        "WdfUsbInterfaceSelectSetting failed");

    WDF_USB_PIPE_INFORMATION pipeInfo;
    WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);

    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(WdfUsbInterfaceGetNumConfiguredPipes(m_ControlInterface) == 1,
                                      STATUS_DEVICE_HARDWARE_ERROR,
                                      "Bad NCM control interface");

    m_ControlInterruptPipe = WdfUsbInterfaceGetConfiguredPipe(m_ControlInterface,
                                                                0, //PipeIndex,
                                                                &pipeInfo);

    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(pipeInfo.PipeType == WdfUsbPipeTypeInterrupt,
                                      STATUS_DEVICE_HARDWARE_ERROR,
                                      "Bad NCM control pipe type");

    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(m_ControlInterruptPipe);
    m_ControlInterruptPipeMaxPacket = pipeInfo.MaximumPacketSize;

    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(WdfUsbInterfaceGetNumConfiguredPipes(m_DataInterface) == 2,
                                      STATUS_DEVICE_HARDWARE_ERROR,
                                      "Bad NCM data interface");

    for (UCHAR pipeIndex = 0; pipeIndex < 2; pipeIndex++)
    {
        WDFUSBPIPE pipe = WdfUsbInterfaceGetConfiguredPipe(m_DataInterface,
                                                            pipeIndex, //PipeIndex,
                                                            &pipeInfo);

        NCM_RETURN_NT_STATUS_IF_FALSE_MSG(pipeInfo.PipeType == WdfUsbPipeTypeBulk,
                                          STATUS_DEVICE_HARDWARE_ERROR,
                                          "Bad NCM data pipe type");

        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

        if (WdfUsbTargetPipeIsInEndpoint(pipe))
        {
            //TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL,
            //    "BulkInput Pipe is 0x%p\n", pipe);

            m_DataBulkInPipe = pipe;
        }
        else if (WdfUsbTargetPipeIsOutEndpoint(pipe))
        {
            //TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL,
            //    "BulkOutput Pipe is 0x%p\n", pipe);

            m_DataBulkOutPipe = pipe;
        }
        else
        {
            NCM_RETURN_NT_STATUS_IF_FALSE_MSG(FALSE,
                                              STATUS_DEVICE_HARDWARE_ERROR,
                                              "Bad NCM data pipe type - unknown");
        }
    }

    //
    // If we didn't find all the 3 pipes, fail the start.
    //

    NCM_RETURN_NT_STATUS_IF_FALSE_MSG(m_ControlInterruptPipe && m_DataBulkInPipe && m_DataBulkOutPipe,
                                      STATUS_DEVICE_HARDWARE_ERROR,
                                      "Bad NCM pipes incomplete");

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
VOID
UsbNcmHostDevice::ControlInterruptPipeReadCompletetionRoutine(
    _In_  WDFUSBPIPE,
    _In_  WDFMEMORY memory,
    _In_  size_t numBytesTransfered,
    _In_  WDFCONTEXT context)
{
    UsbNcmHostDevice* ncmDevice = (UsbNcmHostDevice*) context;

    if (numBytesTransfered < sizeof(USB_CDC_NOTIFICATION))
    {
        // Log trace msg
        return;
    }

    PUSB_CDC_NOTIFICATION cdcNotification = 
        (PUSB_CDC_NOTIFICATION) WdfMemoryGetBuffer(memory, nullptr);

    switch (cdcNotification->bNotificationCode)
    {
        case USB_CDC_NOTIFICATION_NETWORK_CONNECTION:
        {
            ncmDevice->m_NcmAdapterCallbacks->EvtUsbNcmAdapterSetLinkState(
                ncmDevice->m_NetAdapter,
                !!cdcNotification->wValue);

            break;
        }
        case USB_CDC_NOTIFICATION_CONNECTION_SPEED_CHANGE:
        {
            PCDC_CONN_SPEED_CHANGE cdcSpeedChange =
                (PCDC_CONN_SPEED_CHANGE) cdcNotification;

            ncmDevice->m_NcmAdapterCallbacks->EvtUsbNcmAdapterSetLinkSpeed(
                ncmDevice->m_NetAdapter,
                cdcSpeedChange->USBitRate,
                cdcSpeedChange->DSBITRate);

            break;
        }
        default:
            //TODO: Log message for unsupported type
            break;
    }
}

_Use_decl_annotations_
VOID
UsbNcmHostDevice::DataBulkInPipeReadCompletetionRoutine(
    _In_  WDFUSBPIPE,
    _In_  WDFMEMORY memory,
    _In_  size_t numBytesTransferred,
    _In_  WDFCONTEXT context)
{
    UsbNcmHostDevice* hostDevice = (UsbNcmHostDevice*) context;

    NT_FRE_ASSERT(hostDevice->m_HostSelectedNtbInMaxSize >= (UINT32) numBytesTransferred);

    hostDevice->m_NcmAdapterCallbacks->EvtUsbNcmAdapterNotifyReceive(
        hostDevice->m_NetAdapter,
        nullptr,
        0,
        memory,
        WDF_NO_HANDLE);
}

PAGED
_Use_decl_annotations_
void
UsbNcmHostDevice::StartReceive(_In_ WDFDEVICE usbNcmWdfDevice)
{
    StartPipe(NcmGetHostDeviceFromHandle(usbNcmWdfDevice)->m_DataBulkInPipe);
}

PAGED
_Use_decl_annotations_
void
UsbNcmHostDevice::StopReceive(_In_ WDFDEVICE usbNcmWdfDevice)
{
    StopPipe(NcmGetHostDeviceFromHandle(usbNcmWdfDevice)->m_DataBulkInPipe);
}

PAGED
_Use_decl_annotations_
void
UsbNcmHostDevice::StartTransmit(_In_ WDFDEVICE usbNcmWdfDevice)
{
    StartPipe(NcmGetHostDeviceFromHandle(usbNcmWdfDevice)->m_DataBulkOutPipe);
}

PAGED
_Use_decl_annotations_
void
UsbNcmHostDevice::StopTransmit(_In_ WDFDEVICE usbNcmWdfDevice)
{
    StopPipe(NcmGetHostDeviceFromHandle(usbNcmWdfDevice)->m_DataBulkOutPipe);
}

_Use_decl_annotations_
inline
void
UsbNcmHostDevice::TransmitFramesCompetion(
    _In_ WDFREQUEST,
    _In_ WDFIOTARGET target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS,
    _In_ WDFCONTEXT context)
{
    UsbNcmHostDevice* hostDevice = NcmGetHostDeviceFromHandle(WdfIoTargetGetDevice(target));

    hostDevice->m_NcmAdapterCallbacks->EvtUsbNcmAdapterNotifyTransmitCompletion(
        hostDevice->m_NetAdapter,
        (TX_BUFFER_REQUEST*) context);
}

_Use_decl_annotations_
NTSTATUS
UsbNcmHostDevice::TransmitFrames(_In_ WDFDEVICE usbNcmWdfDevice,
                                 _In_ TX_BUFFER_REQUEST* bufferRequest)
{
    NTSTATUS status = STATUS_SUCCESS;
    UsbNcmHostDevice* hostDevice = NcmGetHostDeviceFromHandle(usbNcmWdfDevice);

    NT_FRE_ASSERT(bufferRequest->TransferLength > 0);

    WdfRequestSetCompletionRoutine(bufferRequest->Request,
                                   UsbNcmHostDevice::TransmitFramesCompetion,
                                   bufferRequest);

    WDFMEMORY_OFFSET offset{ 0, bufferRequest->TransferLength };
    status = WdfUsbTargetPipeFormatRequestForWrite(hostDevice->m_DataBulkOutPipe,
                                                   bufferRequest->Request,
                                                   bufferRequest->BufferWdfMemory,
                                                   &offset);

    if (NT_SUCCESS(status))
    {
        WDF_REQUEST_SEND_OPTIONS sendOptions = {};
        WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
        WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions, WDF_REL_TIMEOUT_IN_SEC(5));

        if (!WdfRequestSend(bufferRequest->Request,
                            WdfUsbTargetPipeGetIoTarget(hostDevice->m_DataBulkOutPipe), &sendOptions))
        {
            status = WdfRequestGetStatus(bufferRequest->Request);
        }
    }

    return status;
}
