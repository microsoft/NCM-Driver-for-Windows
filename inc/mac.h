// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#define MAC_ADDRESS_TO_NUMBER(_NA)  \
    ((UINT64)((_NA)[5])         |   \
    (UINT64)((_NA)[4]) << 8     |   \
    (UINT64)((_NA)[3]) << 16    |   \
    (UINT64)((_NA)[2]) << 24    |   \
    (UINT64)((_NA)[1]) << 32    |   \
    (UINT64)((_NA)[0]) << 40)

#define NUMBER_TO_MAC_ADDRESS(_D, _S)                      \
    ((_D)[5]) = (UCHAR)((_S) & 0xFFull);                   \
    ((_D)[4]) = (UCHAR)(((_S) & 0xFF00ull) >> 8);          \
    ((_D)[3]) = (UCHAR)(((_S) & 0xFF0000ull) >> 16);       \
    ((_D)[2]) = (UCHAR)(((_S) & 0xFF000000ull) >> 24);     \
    ((_D)[1]) = (UCHAR)(((_S) & 0xFF00000000ull) >> 32);   \
    ((_D)[0]) = (UCHAR)(((_S) & 0xFF0000000000ull) >> 40);

static const unsigned char s_MinMacAddress[ETH_LENGTH_OF_ADDRESS] =
{ 0x02, 0x15, 0x5d, 0x00, 0x00, 0x00 };
static const UINT64 s_MinMacNumber = 0x000002155d000000;

static const unsigned char s_MaxMacAddress[ETH_LENGTH_OF_ADDRESS] =
{ 0x02, 0x15, 0x5d, 0xff, 0xff, 0xff };
static const UINT64 s_MaxMacNumber = 0x000002155dffffff;

inline
void
GenerateMACAddress(
    _Out_writes_bytes_(ETH_LENGTH_OF_ADDRESS) unsigned char* MacAddress
)
{
    LARGE_INTEGER seed;
    KeQueryTickCount(&seed);

    UINT64 newMacNumber
        = (seed.QuadPart) % (s_MaxMacNumber - s_MinMacNumber) + s_MinMacNumber;

    NUMBER_TO_MAC_ADDRESS(MacAddress, newMacNumber);
}

inline
void
GetNextMACAddress(
    _Inout_updates_bytes_(ETH_LENGTH_OF_ADDRESS) unsigned char* MacAddress
)
{
    UINT64 currMacNumber = MAC_ADDRESS_TO_NUMBER(MacAddress);

    if (++currMacNumber > s_MaxMacNumber)
    {
        currMacNumber = s_MinMacNumber;
    }

    NUMBER_TO_MAC_ADDRESS(MacAddress, currMacNumber);
}

class UNIQUE_WDFMEMORY
{
public:

    UNIQUE_WDFMEMORY()
        : m_WdfMemory(nullptr)
    {
    }

    ~UNIQUE_WDFMEMORY()
    {
        if (m_WdfMemory != nullptr)
        {
            WdfObjectDelete(m_WdfMemory);
        }

        m_WdfMemory = nullptr;
    }

    WDFMEMORY* Address()
    {
        NT_FRE_ASSERT(m_WdfMemory == nullptr);

        return &m_WdfMemory;
    }

    WDFMEMORY Get()
    {
        return m_WdfMemory;
    }

    PVOID GetBuffer()
    {
        return WdfMemoryGetBuffer(m_WdfMemory, nullptr);
    }

    WDFMEMORY m_WdfMemory;
};

inline
void
BytesToHexString(
    _In_bytecount_(StrLength / sizeof(WCHAR)) const BYTE* Bytes,
    _In_ ULONG StrLength,
    _Out_writes_(StrLength) WCHAR* String
)
{
    NT_FRE_ASSERT((StrLength % sizeof(WCHAR) == 0) &&
                  (Bytes != nullptr) &&
                  (String != nullptr));

    for (size_t i = 0; i < StrLength / sizeof(WCHAR); i++)
    {
        BYTE lowerBits = Bytes[i] & 0xF;
        BYTE higherBits = (Bytes[i] >> 4) & 0xF;

        String[sizeof(WCHAR) * i] = (higherBits >= 0xA) ? (L'A' + higherBits - 0xA) : (L'0' + higherBits);
        String[sizeof(WCHAR) * i + 1] = (lowerBits >= 0xA) ? (L'A' + lowerBits - 0xA) : (L'0' + lowerBits);
    }
}

inline
bool
HexNibbleFromChar(
    _In_ WCHAR wch,
    _Out_ BYTE &b)
{
    if (wch >= L'A' && wch <= L'F')
    {
        b = static_cast<BYTE>(wch - L'A' + 0xA);
    }
    else if (wch >= L'0' && wch <= L'9')
    {
        b = static_cast<BYTE>(wch) - L'0';
    }
    else
    {
        return false;
    }

    return true;
}

inline
NTSTATUS
HexStringToBytes(
    _In_ LPCWSTR pDigits,
    _Out_writes_(cb) BYTE * pb,
    _In_ size_t cb
)
{
    for (UINT i = 0; i < cb; i++)
    {
        BYTE bLeft, bRight;

        if (!HexNibbleFromChar(pDigits[0], bLeft))
            return STATUS_BAD_DATA;

        if (!HexNibbleFromChar(pDigits[1], bRight))
            return STATUS_BAD_DATA;

        //byte order is unchanged
        pb[i] = bRight | (bLeft << 4);
        pDigits += 2;
    }

    return STATUS_SUCCESS;
}
