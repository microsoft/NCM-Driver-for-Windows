// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <Modules.Library\DmfModules.Library.h>
#include <netadaptercx.h>
#include "device.h"

EXTERN_C_START

// WDFDRIVER Events

DRIVER_INITIALIZE
    DriverEntry;

EVT_WDF_OBJECT_CONTEXT_CLEANUP
    UsbNcmFunctionEvtDriverContextCleanup;

EVT_WDF_DRIVER_DEVICE_ADD
    UsbNcmFunctionEvtDeviceAdd;

EVT_WDF_OBJECT_CONTEXT_CLEANUP
    UsbNcmFunctionEvtDriverContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE
    UsbNcmFunctionEvtDevicePrepareHardware;

EVT_WDF_DEVICE_RELEASE_HARDWARE
    UsbNcmFunctionEvtDeviceReleaseHardware;

EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT
    UsbNcmFunctionEvtDeviceSelfManagedIoInitAndRestart;

EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND
    UsbNcmFunctionEvtDeviceSelfManagedIoSuspend;

EXTERN_C_END
