// Copyright (C) Microsoft Corporation. All rights reserved.

#include "driver.h"
#include "driver.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, UsbNcmHostEvtDeviceAdd)
#pragma alloc_text (PAGE, UsbNcmHostEvtDriverContextCleanup)
#pragma alloc_text (PAGE, UsbNcmHostEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, UsbNcmHostEvtDeviceReleaseHardware)
#endif

ULONG
    g_AdapterVendorId = 0x00155dee;

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

    FuncEntry(USBNCM_HOST);

    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = UsbNcmHostEvtDriverContextCleanup;
    WDF_DRIVER_CONFIG_INIT(&config, UsbNcmHostEvtDeviceAdd);

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
VOID
UsbNcmHostEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
)
{
    FuncEntry(USBNCM_HOST);

    // Stop WPP Tracing
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER) DriverObject));
}

_Use_decl_annotations_
NTSTATUS
UsbNcmHostEvtDeviceAdd(
    _In_ WDFDRIVER,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES attribs;
    WDFDEVICE wdfDevice = nullptr;

    FuncEntry(USBNCM_HOST);

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        NetDeviceInitConfig(DeviceInit),
        "NetDeviceInitConfig failed");

    // Set pnp power callbacks
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = UsbNcmHostEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = UsbNcmHostEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = UsbNcmHostEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = UsbNcmHostEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    // Create WDF device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attribs, UsbNcmHostDevice);
    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfDeviceCreate(&DeviceInit, &attribs, &wdfDevice),
        "WdfDeviceCreate failed");

    new (NcmGetHostDeviceFromHandle(wdfDevice)) UsbNcmHostDevice(wdfDevice);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
UsbNcmHostEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST,
    _In_ WDFCMRESLIST
)
{
    UsbNcmHostDevice * hostDevice = NcmGetHostDeviceFromHandle(Device);

    NCM_RETURN_IF_NOT_NT_SUCCESS(hostDevice->InitializeDevice());
    NCM_RETURN_IF_NOT_NT_SUCCESS(hostDevice->CreateAdapter());

    return STATUS_SUCCESS;
}

NTSTATUS
UsbNcmHostEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST
)
{
    UsbNcmHostDevice * ncmDevice = NcmGetHostDeviceFromHandle(Device);

    ncmDevice->DestroyAdapter();

    return STATUS_SUCCESS;
}

NTSTATUS
UsbNcmHostEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
)
{
    UsbNcmHostDevice * ncmDevice = NcmGetHostDeviceFromHandle(Device);

    return ncmDevice->EnterWorkingState(PreviousState);
}

NTSTATUS
UsbNcmHostEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE
)
{
    UsbNcmHostDevice * ncmDevice = NcmGetHostDeviceFromHandle(Device);

    return ncmDevice->LeaveWorkingState();
}
