# The NCM Driver for Windows

These sample codes are the basis of the actual implementation of the NCM drivers officially shipped with Windows 11. They provide examples of how to write a [WDF NetAdapterCx NIC driver](https://github.com/Microsoft/Network-Adapter-Class-Extension) for USB based NICs. 

Furthermore, they are good references for understanding the behaviors and the features provided by the Windows NCM host driver, and how it interoperates with other NCM compatible function devices.   

# Code Tour

This project contains two NIC drivers: **UsbNcmSample.sys**, the driver for the USB host side; and **UsbNcmFnSample.sys** for the USB function side. While each driver has distinct codes for dealing with either USB host stack or USB function stack, both share many same codes for the common tasks.

## [adapter](adapter)

This is the static library that uses NetAdapterCx APIs, and in turn interact with the rest of network stack above. It's linked by both the host driver and function driver, and performs tasks such as:
* Create and destory the NetAdapter object
* Configure the NetAdapter using registry settings
* Create and destroy the tx and rx queue objects
* Transmitting and receiving the network packets from/to the network stack above

This library is agnostic about the device stack below. It does not interact directly with either host stack or function stack; instead, it uses a set of common callbacks exposed by the host and function driver, as defined in [inc/callbacks.h](inc/callbacks.h).

## [common](common)

This is the other static library in the project that implements a few common tasks needed by both host and function drivers:
* Packing and unpacking datagrams into/from NTB according to NCM specification. It supports both 16-bit NTB and 32-bit NTB format.
* Manages pre-allocated memory and WDFREQUEST objects so that it avoids dynamic resource allocation

If your NIC uses some other proprietary ways of packing datagrams into transfer blocks, this is there you can replace the sample code with your own implementation. 

## [host](host)

This builds the host driver **UsbNcmSample.sys** binary. It contains USB host stack specific logic and reads NCM descriptors from the attached NCM function devices. It performs the actual data transfer between the adapter object and the USB host stack, and also handles other necessary control messages and interrupts.

The host driver ties the adapter's life-cycle with its device's life-cycle: It creates the adapter in the EvtWdfDevicePrepareHardware callback and destroys the adapter in the EvtWdfDeviceReleaseHardware callback. 

## [function](function)

This builds the function driver **UsbNcmFnSample.sys** binary. It contains USB function stack specific logic and it emulates a NCM function device. It performs the actual data transfer operation between the adapter object and the USB function stack, and it also handles bus event and generates interrupts to the host side.

The function driver manages the adapter's life-cycle differently than the host driver: It creates the adapter when received alt-setting 1 selected bus event, and it destroys the adapter when received alt-setting 0 selected bus event. All above happens when the device is in the fully working state, i.e. after D0Entry

This demonstrates an important aspect of NetAdapterCx framework - the adapter and device can be de-coupled in NetAdapterCx based driver. 

## [inc](inc)

This folder contains various C++ headers included by previously mentioned components, some notable ones are 

* **_callbacks.h_** - declares the callbacks between adpater and device that each side implements
* **_buffers.h_** - declares the pre-allocated memory/WDFREQUEST pool APIs for tx and rx queue 
* **_NetPacketLibrary.h_** - declares a few helper APIs for manipulate the NET_RING/NET_PACKET/NET_FRAGMENT
* **_ntb.h_** - declares NTB packing and unpacking APIs

## [dmf](dmf)

Both drivers, mainly the function driver, leverage certain pre-built modules from [DMF](https://github.com/Microsoft/DMF), so DMF repro is included here as a submodule that the entire project is build-able under Visual Studio

# Contributing

This project welcomes contributions and suggestions. Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit https://cla.microsoft.com.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/). For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.
