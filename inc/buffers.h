// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

DECLARE_HANDLE(TX_BUFFER_REQUEST_POOL);
DECLARE_HANDLE(RX_BUFFER_QUEUE);

struct RX_BUFFER
{
    bool        UseContinuousRequestTarget = false;
    HANDLE      ContinuousRequestTarget = nullptr;
    WDFMEMORY   BufferWdfMemory = nullptr;
    PUCHAR      Buffer = nullptr;
    size_t      BufferSize = 0;
};

struct TX_BUFFER_REQUEST
{
    WDFREQUEST              Request = nullptr;
    size_t                  BufferLength = 0;
    WDFMEMORY               BufferWdfMemory = nullptr;
    size_t                  TransferLength = 0;
    NDIS_STATISTICS_INFO    Stats;
#pragma warning(suppress:4200) //nonstandard extension used: zero-sized array in struct/union
    UCHAR                   Buffer[];
};

PAGED
NTSTATUS TxBufferRequestPoolCreate(
    _In_ WDFDEVICE device,
    _In_ WDFOBJECT parent,
    _In_ size_t bufferSize,
    _Out_ TX_BUFFER_REQUEST_POOL* handle);

PAGED
NTSTATUS RxBufferQueueCreate(
    _In_ WDFDEVICE device,
    _In_ WDFOBJECT parent,
    _Out_ RX_BUFFER_QUEUE* handle);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
TxBufferRequestPoolGetBufferRequest(_In_ TX_BUFFER_REQUEST_POOL handle,
                                    _Outptr_ TX_BUFFER_REQUEST** bufferRequest);

_IRQL_requires_max_(DISPATCH_LEVEL)
void
TxBufferRequestPoolReturnBufferRequest(_In_ TX_BUFFER_REQUEST_POOL handle,
                                       _In_ TX_BUFFER_REQUEST* bufferRequest);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
RxBufferQueueEnqueueBuffer(_In_ RX_BUFFER_QUEUE handle,
                           _In_reads_opt_(bufferSize) PUCHAR buffer,
                           _In_opt_ size_t bufferSize,
                           _In_opt_ WDFMEMORY bufferMemory,
                           _In_opt_ WDFOBJECT returnContext);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
RxBufferQueueDequeueBuffer(_In_ RX_BUFFER_QUEUE handle,
                           _Outptr_ RX_BUFFER** rxBuffer);

_IRQL_requires_max_(DISPATCH_LEVEL)
void
RxBufferQueueReturnBuffer(_In_ RX_BUFFER_QUEUE handle,
                          _In_ RX_BUFFER* rxBuffer);
