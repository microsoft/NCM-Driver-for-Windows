// Copyright (C) Microsoft Corporation. All rights reserved.

#include "driver.h"
#include "driver.tmh"
#include <Modules.Library\DmfModules.Library.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, UsbNcmFunctionEvtDeviceAdd)
#pragma alloc_text (PAGE, UsbNcmFunctionEvtDriverContextCleanup)
#pragma alloc_text (PAGE, UsbNcmFunctionEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, UsbNcmFunctionEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, UsbNcmFunctionEvtDeviceSelfManagedIoInitAndRestart)
#pragma alloc_text (PAGE, UsbNcmFunctionEvtDeviceSelfManagedIoSuspend)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    // Initialize WPP Tracing
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    FuncEntry(USBNCM_FUNCTION);

    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = UsbNcmFunctionEvtDriverContextCleanup;
    WDF_DRIVER_CONFIG_INIT(&config, UsbNcmFunctionEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE);

    if (!NT_SUCCESS(status))
    {
        WPP_CLEANUP(DriverObject);
        NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(status, "WdfDriverCreate failed");
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
void
UsbNcmFunctionEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
)
{
    FuncEntry(USBNCM_FUNCTION);

    // Stop WPP Tracing
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER) DriverObject));
}

_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionEvtDeviceAdd(
    _In_    WDFDRIVER,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES attribs;
    WDFDEVICE wdfDevice = nullptr;

    FuncEntry(USBNCM_FUNCTION);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        NetDeviceInitConfig(DeviceInit),
        "NetDeviceInitConfig failed");

    // Set pnp power callbacks
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = UsbNcmFunctionEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = UsbNcmFunctionEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = UsbNcmFunctionEvtDeviceSelfManagedIoInitAndRestart;
#pragma warning(suppress:28024) // IoInit and IoRestart has exact same function signature, intentionally reusing the same implementation
    pnpPowerCallbacks.EvtDeviceSelfManagedIoRestart = UsbNcmFunctionEvtDeviceSelfManagedIoInitAndRestart;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoSuspend = UsbNcmFunctionEvtDeviceSelfManagedIoSuspend;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    // Create WDF device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attribs, UsbNcmFunctionDevice);
    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfDeviceCreate(&DeviceInit, &attribs, &wdfDevice),
        "WdfDeviceCreate failed");

    new (NcmGetFunctionDeviceFromHandle(wdfDevice)) UsbNcmFunctionDevice(wdfDevice);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
UsbNcmFunctionEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST,
    _In_ WDFCMRESLIST
)
{
    UsbNcmFunctionDevice * functionDevice = NcmGetFunctionDeviceFromHandle(Device);

    NCM_RETURN_IF_NOT_NT_SUCCESS(functionDevice->InitializeDevice());

    return STATUS_SUCCESS;
}

NTSTATUS
UsbNcmFunctionEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST
)
{
    UsbNcmFunctionDevice * functionDevice = NcmGetFunctionDeviceFromHandle(Device);

    functionDevice->UnInitializeDevice();

    return STATUS_SUCCESS;
}

NTSTATUS
UsbNcmFunctionEvtDeviceSelfManagedIoInitAndRestart(
    _In_ WDFDEVICE Device
)
{
    UsbNcmFunctionDevice * functionDevice = NcmGetFunctionDeviceFromHandle(Device);

    NCM_RETURN_IF_NOT_NT_SUCCESS(functionDevice->SubscribeBusEventNotification());

    return STATUS_SUCCESS;
}

NTSTATUS
UsbNcmFunctionEvtDeviceSelfManagedIoSuspend(
    _In_ WDFDEVICE Device
)
{
    UsbNcmFunctionDevice * functionDevice = NcmGetFunctionDeviceFromHandle(Device);

    functionDevice->UnSubscribeBusEventNotification();

    return STATUS_SUCCESS;
}
