// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

DECLARE_HANDLE(NTB_HANDLE);

enum NTB_DIRECTION
{
    NTB_RX = 0x1,
    NTB_TX = 0x2,
};

PAGED
NTSTATUS
NcmTransferBlockCreate(
    _In_ NETPACKETQUEUE netPacketQueue,
    _In_ bool use32bitNtb,
    _In_ UINT16 ntbMaxDatagrams,
    _In_ UINT16 ndpAlignment,
    _In_ UINT16 ndpDivisor,
    _In_ UINT16 ndpPayloadRemainder,
    _Out_ NTB_HANDLE* ntbHandle);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NcmTransferBlockReInitializeBuffer(
    _In_ NTB_HANDLE ntbHandle,
    _In_ const PUCHAR buffer,
    _In_ const size_t bufferLength,
    _In_ NTB_DIRECTION direction);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NcmTransferBlockCopyNextDatagram(
    _In_ NTB_HANDLE ntbHandle,
    _In_ NcmPacketIterator* pi);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NcmTransferBlockSetNdp(_In_ NTB_HANDLE ntbHandle,
                       _Out_ size_t* transferLength);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NcmTransferBlockGetNextDatagram(
    _In_ NTB_HANDLE ntbHandle,
    _Outptr_result_buffer_(*datagramBufferSize) PUCHAR* datagramBuffer,
    _Out_ size_t* datagramBufferSize);

