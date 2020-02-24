// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <netadaptercx.h>
#include "ncmringiterator.h"
#include "trace.h"
#include "ncm.h"
#include "ntb.h"
#include "ntb.tmh"

typedef const UINT32 NTH_SIG;
typedef NTH_SIG NDP_SIG;

NTH_SIG NTH16_SIGNATURE = 'HMCN';       //'NCMH'
NTH_SIG NTH32_SIGNATURE = 'hmcn';       //'ncmh'

NDP_SIG NDP16_SIGNATURE = '0MCN';       //'NCM0'
NDP_SIG NDP16_CRC_SIGNATURE = '1MCN';   //'NCM1'

NDP_SIG NDP32_SIGNATURE = '0mcn';       //'ncm0'
NDP_SIG NDP32_CRC_SIGNATURE = '1mcn';   //'ncm1'

template <
    typename NTH,           //Ncm Transfer Header (16 bit or 32 bit)
    typename NDP,           //Ncm Datagram Pointer Table Header (16 bit or 32 bit)
    typename DPE,           //Ncm Datagram Pointer Entry (16 bit or 32 bit)
    typename PTR_SIZE,      //16 bit or 32 bit
    NTH_SIG nth_sig,
    NDP_SIG ndp_sig,
    NDP_SIG ndp_crc_sig> class NcmTransferBlock
{
public:
    BOOLEAN             m_Is32bitNtb = FALSE;

private:

    // the following fields are initialized once
    UINT16              m_Sequence = 0;
    UINT16              m_NtbMaxDatagrams = 0;
    UINT16              m_NdpAlignment = 0;
    UINT16              m_NdpDivisor = 0;
    UINT16              m_NdpPayloadRemainder = 0;
    WDFMEMORY           m_DatagramPointerTableMemory = nullptr;

    // the following fields are reinitialized everytime when
    // a new buffer is used by calling InitializeBuffer
    PUCHAR              m_Buffer = nullptr;
    size_t              m_BufferLength = 0;
    UINT32              m_BlockLength = 0;
    UINT32              m_CurrentNdpIndex = 0;
    UINT32              m_NextNdpIndex = 0;
    UINT32              m_CurrentNdpDatagramIndex = 0;
    UINT32              m_CurrentNdpDatagramCount = 0;
    size_t              m_CurrentBufferOffset = 0;
    DPE*                m_DatagramPointerTable = nullptr;

public:

    PAGED
    NTSTATUS InitializeNtb(_In_ NETPACKETQUEUE netPacketQueue,
                           _In_ UINT16 ntbMaxDatagrams,
                           _In_ UINT16 ndpAlignment,
                           _In_ UINT16 ndpDivisor,
                           _In_ UINT16 ndpPayloadRemainder,
                           _In_ bool use32bitNtb)
    {
        m_Sequence = 0;

        NT_FRE_ASSERT(ntbMaxDatagrams > 0);
        m_NtbMaxDatagrams = ntbMaxDatagrams;

        m_NdpAlignment = ndpAlignment;
        m_NdpDivisor = ndpDivisor;
        m_NdpPayloadRemainder = ndpPayloadRemainder;
        m_Is32bitNtb = use32bitNtb;

        WDF_OBJECT_ATTRIBUTES objectAttribs;
        WDF_OBJECT_ATTRIBUTES_INIT(&objectAttribs);
        objectAttribs.ParentObject = netPacketQueue;

        NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
            WdfMemoryCreate(&objectAttribs,
                            NonPagedPoolNx,
                            0,
                            sizeof(DPE) * (m_NtbMaxDatagrams + 1),
                            &m_DatagramPointerTableMemory,
                            nullptr),
            "WdfMemoryCreate failed");

        return STATUS_SUCCESS;
    }

    _IRQL_requires_max_(DISPATCH_LEVEL)
    NTSTATUS ReInitializeBuffer(_In_ const PUCHAR buffer,
                                _In_ const size_t bufferLength,
                                _In_ NTB_DIRECTION direction)
    {
        m_BlockLength = 0;
        m_CurrentNdpIndex = 0;
        m_NextNdpIndex = 0;
        m_CurrentNdpDatagramIndex = 0;
        m_CurrentNdpDatagramCount = 0;
        m_CurrentBufferOffset = 0;

        if (direction == NTB_RX)
        {
            return PrepareToParse(buffer,
                                  bufferLength);
        }
        else
        {
            PrepareToConstruct(buffer,
                               bufferLength);

            return STATUS_SUCCESS;
        }
    }

    _IRQL_requires_max_(DISPATCH_LEVEL)
    NTSTATUS CopyNextDatagram(_In_ NcmPacketIterator* pi)
    {
        if (m_CurrentNdpDatagramCount >= m_NtbMaxDatagrams)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        size_t datagramLength = NcmGetPacketDataLength(pi);

        //
        // NCM Rev 1.0 (Errata 1) 3.3.4
        // 
        // Alignment requirements are met by controlling the location of the payload (the data 
        // following the Ethernet header in each datagram).  This alignment is specified by 
        // indicating a constraint as a divisor and a remainder. 
        //

        // the new datagram starts at current offset + alignemnt needed
        size_t datagramOffset =
            ALIGN_UP_BY(m_CurrentBufferOffset + sizeof(ETHERNET_HEADER), m_NdpDivisor)
            + m_NdpPayloadRemainder - sizeof(ETHERNET_HEADER);

        //
        // NCM Rev 1.0 (Errata 1) 6.2.1
        // 
        // NDP alignment modulus for use in NTBs on the OUT pipe
        //

        // NDP offset starts after the new datagram + alignment needed
        size_t ndpOffset = ALIGN_UP_BY(datagramOffset + datagramLength, m_NdpAlignment);

        //
        // NCM Rev 1.0 (Errata 1) 3.7
        // 
        // that every NDP shall end with a Null entry
        //
        size_t totalNtbSizeNeeded =
            ndpOffset + sizeof(NDP) + (m_CurrentNdpDatagramCount + 1) * sizeof(DPE);

        if (totalNtbSizeNeeded > m_BufferLength)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        PUCHAR datagram = m_Buffer + datagramOffset;

        NcmCopyPacketDataToBuffer(datagram, pi, datagramLength);

        m_DatagramPointerTable[m_CurrentNdpDatagramCount].DatagramIndex = (PTR_SIZE)datagramOffset;
        m_DatagramPointerTable[m_CurrentNdpDatagramCount].DatagramLength = (PTR_SIZE)datagramLength;

        m_CurrentNdpDatagramCount++;
        m_CurrentBufferOffset = datagramOffset + datagramLength;

        return STATUS_SUCCESS;
    }

    _IRQL_requires_max_(DISPATCH_LEVEL)
    void SetNdp(_Out_ size_t* transferLength)
    {
        NT_FRE_ASSERT(m_CurrentNdpDatagramCount > 0);

        //
        // NCM Rev 1.0 (Errata 1) 6.2.1
        // 
        // NDP alignment modulus for use in NTBs on the OUT pipe
        //

        // NDP offset starts after the new datagram + alignment needed
        size_t ndpOffset = ALIGN_UP_BY(m_CurrentBufferOffset, m_NdpAlignment);

        //
        // NCM Rev 1.0 (Errata 1) 3.7
        // 
        // that every NDP shall end with a Null entry
        //

        // including the null teminitor entry
        size_t datagramPointerTableSize = (m_CurrentNdpDatagramCount + 1) * sizeof(DPE);

        NDP& ndpHeader = (NDP&) *(m_Buffer + ndpOffset);
        ndpHeader.Signature = ndp_sig;
        ndpHeader.Length = (UINT16)(sizeof(NDP) + datagramPointerTableSize);
        ndpHeader.NextNdpIndex = 0;

        RtlCopyMemory(m_Buffer + ndpOffset + sizeof(NDP),
                      m_DatagramPointerTable,
                      datagramPointerTableSize);

        NTH& ntbHeader = (NTH&)*m_Buffer;

        ntbHeader.Signature = nth_sig;
        ntbHeader.HeaderLength = sizeof(NTH);
        ntbHeader.Sequence = m_Sequence;
        ntbHeader.BlockLength = (PTR_SIZE)(ndpOffset + ndpHeader.Length);
        ntbHeader.NdpIndex = (PTR_SIZE)ndpOffset;

        *transferLength = ntbHeader.BlockLength;
    }

    _IRQL_requires_max_(DISPATCH_LEVEL)
    NTSTATUS GetNextDatagram(
        _Outptr_result_buffer_(*datagramBufferSize) PUCHAR* datagramBuffer,
        _Out_ size_t* datagramBufferSize)
    {
        if (m_CurrentNdpDatagramIndex == m_CurrentNdpDatagramCount)
        {
            NTSTATUS status = GetNextNdp();

            if (STATUS_SUCCESS != status)
            {
                return status;
            }
        }

        const DPE& datagram =
            (const DPE&) *(m_Buffer + m_CurrentNdpIndex + sizeof(NDP) + m_CurrentNdpDatagramIndex * sizeof(DPE));

        *datagramBuffer = m_Buffer + datagram.DatagramIndex;
        *datagramBufferSize = datagram.DatagramLength;
        m_CurrentNdpDatagramIndex++;

        return STATUS_SUCCESS;
    }

private:

    _IRQL_requires_max_(DISPATCH_LEVEL)
    NTSTATUS PrepareToParse(_In_ const PUCHAR buffer,
                            _In_ const size_t bufferLength)
    {
        m_Buffer = buffer;
        m_BufferLength = bufferLength;

        const NTH& ntbHeader = (const NTH&) *m_Buffer;

        if ((m_BufferLength >= sizeof(NTH)) &&
            (nth_sig == ntbHeader.Signature))
        {
            if (ntbHeader.HeaderLength != sizeof(NTH))
            {
                NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NTB header length");
            }

            if (ntbHeader.BlockLength > m_BufferLength)
            {
                NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NTB header block length");
            }

            if (ntbHeader.BlockLength > 0)
            {
                m_BlockLength = ntbHeader.BlockLength;
            }
            else
            {
                m_BlockLength = min(((PTR_SIZE) ~((PTR_SIZE) 0)), // MAXUINT16 or MAXUINT32
                    (PTR_SIZE) m_BufferLength);
            }

            m_NextNdpIndex = ntbHeader.NdpIndex;

            // validate the datagram pointer table
            return GetNextNdp();
        }

        NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NTB header signature");

        return STATUS_BAD_DATA;
    };

    _IRQL_requires_max_(DISPATCH_LEVEL)
    void PrepareToConstruct(_In_ const PUCHAR buffer,
                            _In_ const size_t bufferLength)
    {
        m_Buffer = buffer;
        m_BufferLength = bufferLength;

        m_Sequence++;
        m_CurrentBufferOffset = sizeof(NTH);

        size_t size;
        m_DatagramPointerTable = (DPE*) WdfMemoryGetBuffer(m_DatagramPointerTableMemory, &size);
        RtlZeroMemory(m_DatagramPointerTable, size);
    }

    _IRQL_requires_max_(DISPATCH_LEVEL)
    NTSTATUS GetNextNdp()
    {
        const size_t minDptSize = sizeof(NDP) + 2 * sizeof(DPE);

        if (m_NextNdpIndex == 0)
        {
            return STATUS_NO_MORE_ENTRIES;
        }

        m_CurrentNdpDatagramIndex = 0;
        m_CurrentNdpDatagramCount = 0;

        if ((m_NextNdpIndex < sizeof(NTH)) ||
            ((m_NextNdpIndex + minDptSize) > m_BlockLength))
        {
            NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NDP size #1");
        }

        const NDP& ndpHeader = (const NDP&) *(m_Buffer + m_NextNdpIndex);

        if (ndp_sig != ndpHeader.Signature)
        {
            NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NDP signature");
        }

        if ((ndpHeader.Length < minDptSize) ||
            ((ndpHeader.Length & 0x3) != 0) ||
            ((m_NextNdpIndex + ndpHeader.Length) > m_BlockLength))
        {
            NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NDP size #2");
        }

        if ((ndpHeader.NextNdpIndex != 0) &&
            ((ndpHeader.NextNdpIndex < sizeof(NTH)) ||
            ((ndpHeader.NextNdpIndex + minDptSize) > m_BlockLength)))
        {
            NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NDP size #3");
        }

        // scan the table's datagram entries for validity
        // note: if any invalid entries are found, the entire table, and any subsequent tables, are rejected
        UINT32 currentNdpDatagramCount = 0;

        for (size_t offset = sizeof(NDP);
             offset < ndpHeader.Length;
             offset += sizeof(DPE))
        {
            const DPE& datagram = (const DPE&) *(m_Buffer + m_NextNdpIndex + offset);

            if (datagram.DatagramIndex == 0 || datagram.DatagramLength == 0)
            {
                m_CurrentNdpIndex = m_NextNdpIndex;
                m_NextNdpIndex = ndpHeader.NextNdpIndex;
                m_CurrentNdpDatagramCount = currentNdpDatagramCount;

                return STATUS_SUCCESS;
            }

            if ((datagram.DatagramIndex < sizeof(NTH)) ||
                (datagram.DatagramIndex > m_BlockLength) ||
                (((UINT64) datagram.DatagramIndex + (UINT64) datagram.DatagramLength) > m_BlockLength))
            {
                NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NDP that not terminates");
            }

            currentNdpDatagramCount++;
        }

        NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(STATUS_BAD_DATA, "Bad NDP size #4");

        return STATUS_BAD_DATA;
    };
};

typedef
NcmTransferBlock<
    NcmTransferHeader16,
    NcmDatagramPointerTable16,
    NcmDatagramPointer16,
    UINT16,
    NTH16_SIGNATURE,
    NDP16_SIGNATURE,
    NDP16_CRC_SIGNATURE> NcmTransferBlock16;

typedef
NcmTransferBlock<
    NcmTransferHeader32,
    NcmDatagramPointerTable32,
    NcmDatagramPointer32,
    UINT32,
    NTH32_SIGNATURE,
    NDP32_SIGNATURE,
    NDP32_CRC_SIGNATURE> NcmTransferBlock32;

PAGEDX
_Use_decl_annotations_
NTSTATUS
NcmTransferBlockCreate(
    _In_ NETPACKETQUEUE netPacketQueue,
    _In_ bool use32bitNtb,
    _In_ UINT16 ntbMaxDatagrams,
    _In_ UINT16 ndpAlignment,
    _In_ UINT16 ndpDivisor,
    _In_ UINT16 ndpPayloadRemainder,
    _Out_ NTB_HANDLE* ntbHandle)
{
    WDF_OBJECT_ATTRIBUTES  objectAttribs;
    WDFMEMORY              wdfMemory;
    PVOID                  handle;

    *ntbHandle = nullptr;

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttribs);
    objectAttribs.ParentObject = netPacketQueue;

    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        WdfMemoryCreate(&objectAttribs,
                        NonPagedPoolNx,
                        0,
                        use32bitNtb ? sizeof(NcmTransferBlock32) : sizeof(NcmTransferBlock16),
                        &wdfMemory,
                        (PVOID*) &handle),
        "WdfMemoryCreate failed");

    if (use32bitNtb)
    {
        NCM_RETURN_IF_NOT_NT_SUCCESS(
            ((NcmTransferBlock32*) handle)->InitializeNtb(netPacketQueue,
                                                          ntbMaxDatagrams,
                                                          ndpAlignment,
                                                          ndpDivisor,
                                                          ndpPayloadRemainder,
                                                          true));
    }
    else
    {
        NCM_RETURN_IF_NOT_NT_SUCCESS(
            ((NcmTransferBlock16*) handle)->InitializeNtb(netPacketQueue,
                                                          ntbMaxDatagrams,
                                                          ndpAlignment,
                                                          ndpDivisor,
                                                          ndpPayloadRemainder,
                                                          false));
    }

    *ntbHandle = (NTB_HANDLE) handle;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
NcmTransferBlockReInitializeBuffer(
    _In_ NTB_HANDLE ntbHandle,
    _In_ const PUCHAR buffer,
    _In_ const size_t bufferLength,
    _In_ NTB_DIRECTION direction)
{
    if (((NcmTransferBlock16*) ntbHandle)->m_Is32bitNtb)
    {
        return ((NcmTransferBlock32*) ntbHandle)->ReInitializeBuffer(buffer,
                                                                     bufferLength,
                                                                     direction);
    }
    else
    {
        return ((NcmTransferBlock16*) ntbHandle)->ReInitializeBuffer(buffer,
                                                                     bufferLength,
                                                                     direction);
    }
}

_Use_decl_annotations_
NTSTATUS
NcmTransferBlockCopyNextDatagram(
    _In_ NTB_HANDLE ntbHandle,
    _In_ NcmPacketIterator* pi)
{
    if (((NcmTransferBlock16*) ntbHandle)->m_Is32bitNtb)
    {
        return ((NcmTransferBlock32*) ntbHandle)->CopyNextDatagram(pi);
    }
    else
    {
        return ((NcmTransferBlock16*) ntbHandle)->CopyNextDatagram(pi);
    }
}

_Use_decl_annotations_
void
NcmTransferBlockSetNdp(
    _In_ NTB_HANDLE ntbHandle,
    _Out_ size_t* transferLength)
{
    if (((NcmTransferBlock16*) ntbHandle)->m_Is32bitNtb)
    {
        ((NcmTransferBlock32*) ntbHandle)->SetNdp(transferLength);
    }
    else
    {
        ((NcmTransferBlock16*) ntbHandle)->SetNdp(transferLength);
    }
}

_Use_decl_annotations_
NTSTATUS
NcmTransferBlockGetNextDatagram(
    _In_ NTB_HANDLE ntbHandle,
    _Outptr_result_buffer_(*datagramBufferSize) PUCHAR* datagramBuffer,
    _Out_ size_t* datagramBufferSize)
{
    if (((NcmTransferBlock16*) ntbHandle)->m_Is32bitNtb)
    {
        return ((NcmTransferBlock32*) ntbHandle)->GetNextDatagram(datagramBuffer,
                                                                  datagramBufferSize);
    }
    else
    {
        return ((NcmTransferBlock16*) ntbHandle)->GetNextDatagram(datagramBuffer,
                                                                  datagramBufferSize);
    }
}
