// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma code_seg(push)
#pragma code_seg("PAGE")

#pragma pack(push,1)

union _DL_EUI48
{
    UINT8
        Byte[6];
};

typedef union _DL_EUI48 DL_EUI48, *PDL_EUI48;

typedef struct _ETHERNET_HEADER
{
    DL_EUI48
        Destination;

    DL_EUI48
        Source;

    union
    {
        UINT16
            Type;            // Ethernet

        UINT16
            Length;          // IEEE 802
    };
} ETHERNET_HEADER, *PETHERNET_HEADER;

C_ASSERT(sizeof(ETHERNET_HEADER) == 14);

struct NcmTransferHeader
{
    // dwSignature ("NCMH" (NTH16) or "ncmh" (NTH32))
    UINT32
        Signature;
};

struct NcmTransferHeader16
{
    UINT32    Signature;                          // dwSignature ("NCMH" => NTH16)
    UINT16    HeaderLength;                       // wHeaderLength (== 12)
    UINT16    Sequence;                           // wSequence
    UINT16    BlockLength;                        // wBlockLength
    UINT16    NdpIndex;                           // wNdpIndex
};

struct NcmTransferHeader32
{
    UINT32    Signature;                          // dwSignature ("ncmh" => NTH32)
    UINT16    HeaderLength;                       // wHeaderLength (== 16)
    UINT16    Sequence;                           // wSequence
    UINT32    BlockLength;                        // dwBlockLength
    UINT32    NdpIndex;                           // dwNdpIndex
};

struct NcmDatagramPointer16
{
    UINT16    DatagramIndex;                      // wDatagramIndex
    UINT16    DatagramLength;                     // wDatagramLength
};

struct NcmDatagramPointer32
{
    UINT32    DatagramIndex;                      // dwDatagramIndex
    UINT32    DatagramLength;                     // dwDatagramLength
};

struct NcmDatagramPointerTable16
{
    UINT32    Signature;                          // dwSignature ("NCMx" => NDP16)
    UINT16    Length;                             // wLength
    UINT16    NextNdpIndex;                       // wNextNdpIndex
};

struct NcmDatagramPointerTable32
{
    UINT32    Signature;                          // dwSignature ("ncmx" => NDP32)
    UINT16    Length;                             // wLength
    UINT16    Reserved6;                          // dwReserved6
    UINT32    NextNdpIndex;                       // dwNextNdpIndex
    UINT32    Reserved12;                         // dwReserved12
};

typedef struct _USB_CDC_NOTIFICATION
{
    BYTE    bmRequestType;
    BYTE    bNotificationCode;
    USHORT  wValue;
    USHORT  wIndex;
    USHORT  wLength;

} USB_CDC_NOTIFICATION, *PUSB_CDC_NOTIFICATION;

// CDC 1.2: NetworkConnection
typedef struct _CDC_NETWORK_CONNECTION_STATUS
{
    //bmRequestType     0xA1
    //bNotificationCode 0x00 (NETWORK_CONNECTION)
    //wValue            0 => Disconnected ; 1 => Connected
    //wIndex            Interface
    //wLength           0 (no extra data)
    USB_CDC_NOTIFICATION
        NotificationHeader;

} CDC_NETWORK_CONNECTION_STATUS, *PCDC_NETWORK_CONNECTION_STATUS;

// CDC 1.2: ConnectionSpeedChange
typedef struct _CDC_CONN_SPEED_CHANGE
{
    //bmRequestType     0xA1
    //bNotificationCode 0x2A (CONNECTION_SPEED_CHANGE)
    //wValue            0
    //wIndex            Interface
    //wLength           8 (USBitRate + DSBITRate)

    USB_CDC_NOTIFICATION
        NotificationHeader;

    // upstream bit rate
    UINT32
        USBitRate;

    // downstream bit rate
    UINT32
        DSBITRate;

} CDC_CONN_SPEED_CHANGE, *PCDC_CONN_SPEED_CHANGE;

#define NCM_NETWORK_CONNECTION          0x00        // NETWORK_CONNECTION
#define NCM_RESPONSE_AVAILABLE          0x01        // RESPONSE_AVAILABLE
#define NCM_CONNECTION_SPEED_CHANGE     0x2A        // CONNECTION_SPEED_CHANGE

#define USBFN_NCM_SUPPORT_SET_ETHERNET_PACKET_FILTER 0x1
#define USBFN_NCM_SUPPORT_GET_SET_NET_ADDRESS 0x2
#define USBFN_NCM_SUPPORT_GET_SET_ENCAPSULATED_COMMAND 0x4
#define USBFN_NCM_SUPPORT_GET_SET_MAX_DATAGRAM_SIZE 0x8
#define USBFN_NCM_SUPPORT_GET_SET_CRC_MODE 0x10
#define USBFN_NCM_SUPPORT_GET_SET_8BYTE_NTB_INPUT_SIZE 0x20

typedef struct _NTB_PARAMETERS
{
    UINT16
        wLength;

    UINT16
        bmNtbFormatsSupported;

    UINT32
        dwNtbInMaxSize;

    UINT16
        wNdpInDivisor;

    UINT16
        wNdpInPayloadRemainder;

    UINT16
        wNdpInAlignment;

    UINT16
        wReserved;

    UINT32
        dwNtbOutMaxSize;

    UINT16
        wNdpOutDivisor;

    UINT16
        wNdpOutPayloadRemainder;

    UINT16
        wNdpOutAlignment;

    UINT16
        wNtbOutMaxDatagrams;

} NTB_PARAMETERS, *PNTB_PARAMETERS;

typedef struct _NTB_INPUT_SIZE
{
    UINT32
        dwNtbInMaxSize;

    UINT16
        wNtbInMaxDatagrams;

    UINT16
        wReserved;

} NTB_INPUT_SIZE, *PNTB_INPUT_SIZE;

typedef struct _USB_CDC_UNION_DESCRIPTOR
{
    UCHAR
        bFunctionLength;

    // CS_INTERFACE (USB_CS_INTERFACE_TYPE)
    UCHAR
        bDescriptorType;

    // USB_CS_INTF_SUBTYPE_CDC_UNION
    UCHAR
        bDescriptorSubtype;

    UCHAR
        bControlInterface;

    // variable-length (but for this driver, allow only one)
    UCHAR
        bSubordinateInterface[1];

} USB_CDC_UNION_DESCRIPTOR, *PUSB_CDC_UNION_DESCRIPTOR;

typedef struct _USB_CDC_CS_FUNCTIONAL_DESCRIPTOR
{
    UCHAR
        bFunctionLength;

    // CS_INTERFACE (USB_CS_INTERFACE_TYPE)
    UCHAR
        bDescriptorType;

    // descriptor subtype
    UCHAR
        bDescriptorSubtype;

} USB_CDC_CS_FUNCTIONAL_DESCRIPTOR, *PUSB_CDC_CS_FUNCTIONAL_DESCRIPTOR;

typedef struct _USB_ECM_CS_NET_FUNCTIONAL_DESCRIPTOR
{
    UCHAR
        bFunctionLength;

    // CS_INTERFACE (USB_CS_INTERFACE_TYPE)
    UCHAR
        bDescriptorType;

    // descriptor subtype
    UCHAR
        bDescriptorSubtype;

    // index of MAC address string descriptor
    UCHAR
        iMACAddress;

    // ethernet stats bitmap
    UCHAR
        bmEthernetStatistics[4];

    // maximum segment size
    UINT16
        wMaxSegmentSize;

    // multicast filters bitmap
    UCHAR
        wNumberMCFilters[2];

    // wake-up pattern filter count
    UCHAR
        bNumberPowerFilters;

} USB_ECM_CS_NET_FUNCTIONAL_DESCRIPTOR, *PUSB_ECM_CS_NET_FUNCTIONAL_DESCRIPTOR;

typedef struct _USB_NCM_CS_FUNCTIONAL_DESCRIPTOR
{
    UCHAR
        bFunctionLength;

    // CS_INTERFACE (USB_CS_INTERFACE_TYPE)
    UCHAR
        bDescriptorType;

    // descriptor subtype
    UCHAR
        bDescriptorSubtype;

    UCHAR
        bcdNcmVersion[2];

    // network capabilities bitmap
    UCHAR
        bmNetworkCapabilities;

} USB_NCM_CS_FUNCTIONAL_DESCRIPTOR, *PUSB_NCM_CS_FUNCTIONAL_DESCRIPTOR;

// Capabilities mask definitions for USB_NCM_CS_FUNCTIONAL_DESCRIPTOR.bmNetworkCapabilities
// ref NCM 1.0 5.2.1 (table 5-2) and NCM 1.0 Errata Issue L

#define NCM_FXN_CAP_NTBSIZE_8       0x20    // device can process 8-byte forms of {Get,Set}NtbInputSize
#define NCM_FXN_CAP_CRC_MODE        0x10    // device can process {Set,Get}CrcMode
#define NCM_FXN_CAP_MAX_DGRAM       0x08    // device can process {Set,Get}MaxDatagramSize
#define NCM_FXN_CAP_ENCAPSULATED    0x04    // device can process {SendEncapsulatedCommand, GetEncapsulatedResponse}
#define NCM_FXN_CAP_NET_ADDR        0x02    // device can process {Get,Set}NetAddress
#define NCM_FXN_CAP_PKT_FILTER      0x01    // device can process SetEthernetPacketFilter

typedef struct _USB_CDC_CS_HEADER_DESCRIPTOR
{
    UCHAR
        bFunctionLength;

    // CS_INTERFACE (USB_CS_INTERFACE_TYPE)
    UCHAR
        bDescriptorType;

    // descriptor subtype
    UCHAR
        bDescriptorSubtype;

    // CDC version
    UCHAR
        bcdCDC[2];

} USB_CDC_CS_HEADER_DESCRIPTOR, *PUSB_CDC_CS_HEADER_DESCRIPTOR;

typedef struct _USB_CDC_CS_UNION_DESCRIPTOR
{
    UCHAR
        bFunctionLength;

    // CS_INTERFACE (USB_CS_INTERFACE_TYPE)
    UCHAR
        bDescriptorType;

    // descriptor subtype
    UCHAR
        bDescriptorSubtype;

    // master interface's i/f number
    UCHAR
        bMasterInterface;

    // slave interface set's i/f numbers
    UCHAR
        bSlaveInterfaces[1];

} USB_CDC_CS_UNION_DESCRIPTOR, *PUSB_CDC_CS_UNION_DESCRIPTOR;

enum _USB_CDC_INTERFACE_CLASS
{
    // CDC Spec Version 1.1, Section 4.2, Table 3
    USB_CDC_INTERFACE_CLASS_COMM = 0x02,
    // CDC Spec Version 1.1, Section 4.5, Table 6
    USB_CDC_INTERFACE_CLASS_DATA = 0x0A,
};

enum _USB_CDC_INTERFACE_SUBCLASS
{
    // CDC Spec Version 1.2, Section 4.3, Table 4
    USB_CDC_INTERFACE_SUBCLASS_NCM = 0x0D,
};

enum _USB_DATA_INTERFACE_PROTOCOL
{
    // CDC Spec Version 1.2, Section 4.7, Table 7
    USB_DATA_INTERFACE_PROTOCOL_NCM = 0x01,
};

// CDC-NCM subclass
#define USB_DEVICE_SUBCLASS_CDC_COMM_NCM            0x0D
#define USB_DEVICE_SUBCLASS_CDC_COMM_EEM            0x0C

// class-specific interfaces
#define USB_CS_INTERFACE_TYPE                       0x24
#define USB_CS_CDC_HEADER_TYPE                      0x00
#define USB_CS_CDC_UNION_TYPE                       0x06
#define USB_CS_INTF_SUBTYPE_CDC_UNION               0x06
#define USB_CS_NCM_FUNCTIONAL_DESCR_TYPE            0x1A
#define USB_CS_ECM_FUNCTIONAL_DESCR_TYPE            0x0F

// NCM request types
#define USB_REQUEST_SET_ETHERNET_MULTICAST_FILTERS  0x40    // SET_ETHERNET_MULTICAST_FILTERS
#define USB_REQUEST_SET_ETHERNET_PWR_MGMT_FILTER    0x41    // SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER
#define USB_REQUEST_GET_ETHERNET_PWR_MGMT_FILTER    0x42    // GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER
#define USB_REQUEST_SET_ETHERNET_PACKET_FILTER      0x43    // SET_ETHERNET_PACKET_FILTER
#define USB_REQUEST_GET_ETHERNET_STATISTIC          0x44    // GET_ETHERNET_STATISTIC
#define USB_REQUEST_GET_NTB_PARAMETERS              0x80    // GET_NTB_PARAMETERS
#define USB_REQUEST_GET_NET_ADDRESS                 0x81    // GET_NET_ADDRESS
#define USB_REQUEST_SET_NET_ADDRESS                 0x82    // SET_NET_ADDRESS
#define USB_REQUEST_GET_NTB_FORMAT                  0x83    // GET_NTB_FORMAT
#define USB_REQUEST_SET_NTB_FORMAT                  0x84    // SET_NTB_FORMAT
#define USB_REQUEST_GET_NTB_INPUT_SIZE              0x85    // GET_NTB_INPUT_SIZE
#define USB_REQUEST_SET_NTB_INPUT_SIZE              0x86    // SET_NTB_INPUT_SIZE
#define USB_REQUEST_GET_MAX_DATAGRAM_SIZE           0x87    // GET_MAX_DATAGRAM_SIZE
#define USB_REQUEST_SET_MAX_DATAGRAM_SIZE           0x88    // SET_MAX_DATAGRAM_SIZE
#define USB_REQUEST_GET_CRC_MODE                    0x89    // GET_CRC_MODE
#define USB_REQUEST_SET_CRC_MODE                    0x8A    // SET_CRC_MODE

// ECM packet filter bitmap values
// ref USB ECM 1.2 6.2.4
#define USB_ECM_PACKET_TYPE_MULTICAST               0x10    // PACKET_TYPE_MULTICAST
#define USB_ECM_PACKET_TYPE_BROADCAST               0x08    // PACKET_TYPE_BROADCAST
#define USB_ECM_PACKET_TYPE_DIRECTED                0x04    // PACKET_TYPE_DIRECTED
#define USB_ECM_PACKET_TYPE_ALL_MULTICAST           0x02    // PACKET_TYPE_ALL_MULTICAST
#define USB_ECM_PACKET_TYPE_PROMISCUOUS             0x01    // PACKET_TYPE_PROMISCUOUS

// USB string descriptor index for the ECM MAC address (iMACAddress in the N/W functional descriptor)
#define USB_ECM_MAC_STRING_INDEX                    0x10

#define USB_CDC_NOTIFICATION_NETWORK_CONNECTION (0x00)
#define USB_CDC_NOTIFICATION_RESPONSE_AVAILABLE (0x01)
#define USB_CDC_NOTIFICATION_CONNECTION_SPEED_CHANGE (0x2a)

#pragma pack(pop)
#pragma code_seg(pop)
