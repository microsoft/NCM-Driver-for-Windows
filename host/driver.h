// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <netadaptercx.h>
#include "device.h"

EXTERN_C_START

// WDFDRIVER Events

DRIVER_INITIALIZE
    DriverEntry;

EVT_WDF_OBJECT_CONTEXT_CLEANUP
    UsbNcmHostEvtDriverContextCleanup;

EVT_WDF_DRIVER_DEVICE_ADD
    UsbNcmHostEvtDeviceAdd;

EVT_WDF_OBJECT_CONTEXT_CLEANUP
    UsbNcmHostEvtDriverContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE
    UsbNcmHostEvtDevicePrepareHardware;

EVT_WDF_DEVICE_RELEASE_HARDWARE
    UsbNcmHostEvtDeviceReleaseHardware;

EVT_WDF_DEVICE_D0_ENTRY
    UsbNcmHostEvtDeviceD0Entry;

EVT_WDF_DEVICE_D0_EXIT
    UsbNcmHostEvtDeviceD0Exit;

EXTERN_C_END
