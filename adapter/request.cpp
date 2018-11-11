// Copyright (C) Microsoft Corporation. All rights reserved.

#include "adapter.h"
#include "request.tmh"

typedef struct _NCM_OID_QUERY
{
    NDIS_OID Oid;
    PFN_NET_REQUEST_QUERY_DATA EvtQueryData;
    UINT MinimumSize;

} NCM_OID_QUERY;

typedef struct _NCM_OID_SET
{
    NDIS_OID Oid;
    PFN_NET_REQUEST_SET_DATA EvtSetData;
    UINT MinimumSize;

} NCM_OID_SET;

const NCM_OID_QUERY ComplexQueries[] = {
    { OID_GEN_STATISTICS, NcmAdapter::HandleQueryRequest, sizeof(NDIS_STATISTICS_INFO) },
    { OID_GEN_XMIT_OK, NcmAdapter::HandleQueryRequest, sizeof(UINT64) },
    { OID_GEN_RCV_OK, NcmAdapter::HandleQueryRequest, sizeof(UINT64) },
    { OID_GEN_INTERRUPT_MODERATION, NcmAdapter::HandleQueryRequest, NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1 },
    { OID_GEN_MAXIMUM_TOTAL_SIZE, NcmAdapter::HandleQueryRequest, sizeof(ULONG) }
};

const NCM_OID_SET ComplexSets[] = {
    { OID_GEN_CURRENT_PACKET_FILTER, NcmAdapter::SetPacketFilter, sizeof(ULONG) },
};

PAGED
_Use_decl_annotations_
void
NcmAdapter::HandleQueryRequest(
    _In_ NETREQUESTQUEUE requestQueue,
    _In_ NETREQUEST request,
    _Out_writes_bytes_(outputBufferLength) PVOID outputBuffer,
    _In_ UINT outputBufferLength)
{
    UINT byteWritten = 0;
    NcmAdapter* ncmAdapter = NcmGetAdapterFromHandle(NetRequestQueueGetAdapter(requestQueue));

    switch (NetRequestGetId(request))
    {
        case OID_GEN_XMIT_OK:
        {
            byteWritten = sizeof(UINT64);

            NT_FRE_ASSERT(outputBufferLength >= byteWritten);

            *((PUINT64) outputBuffer) =
                ncmAdapter->m_Stats.ifHCInBroadcastPkts +
                ncmAdapter->m_Stats.ifHCInMulticastPkts +
                ncmAdapter->m_Stats.ifHCInUcastPkts;

            break;
        }
        case OID_GEN_RCV_OK:
        {
            byteWritten = sizeof(UINT64);

            NT_FRE_ASSERT(outputBufferLength >= byteWritten);

            *((PUINT64) outputBuffer) =
                ncmAdapter->m_Stats.ifHCInBroadcastPkts +
                ncmAdapter->m_Stats.ifHCInMulticastPkts +
                ncmAdapter->m_Stats.ifHCInUcastPkts;

            break;
        }
        case OID_GEN_STATISTICS:
        {
            byteWritten = sizeof(NDIS_STATISTICS_INFO);

            NT_FRE_ASSERT(outputBufferLength >= byteWritten);

            ncmAdapter->m_Stats.ifHCInOctets =
                ncmAdapter->m_Stats.ifHCInBroadcastOctets +
                ncmAdapter->m_Stats.ifHCInMulticastOctets +
                ncmAdapter->m_Stats.ifHCInUcastOctets;

            ncmAdapter->m_Stats.ifHCOutOctets =
                ncmAdapter->m_Stats.ifHCOutBroadcastOctets +
                ncmAdapter->m_Stats.ifHCOutMulticastOctets +
                ncmAdapter->m_Stats.ifHCOutUcastOctets;

            RtlCopyMemory(outputBuffer, &ncmAdapter->m_Stats, sizeof(NDIS_STATISTICS_INFO));
            break;
        }
        case OID_GEN_INTERRUPT_MODERATION:
        {
            byteWritten = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;

            NT_FRE_ASSERT(outputBufferLength >= byteWritten);

            NDIS_INTERRUPT_MODERATION_PARAMETERS* params =
                (NDIS_INTERRUPT_MODERATION_PARAMETERS*) outputBuffer;

            params->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            params->Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            params->Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            params->Flags = 0;
            params->InterruptModeration = NdisInterruptModerationNotSupported;

            break;
        }
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        {
            byteWritten = sizeof(ULONG);

            NT_FRE_ASSERT(outputBufferLength >= byteWritten);

            *((PULONG) outputBuffer) = ncmAdapter->m_Parameters.MaxDatagramSize;

            break;
        }
    }

    NetRequestQueryDataComplete(request, STATUS_SUCCESS, byteWritten);
}

PAGED
_Use_decl_annotations_
void
NcmAdapter::SetPacketFilter(
    _In_ NETREQUESTQUEUE requestQueue,
    _In_ NETREQUEST request,
    _In_reads_bytes_(inputBufferLength) PVOID inputBuffer,
    _In_ UINT inputBufferLength)
{
    NT_FRE_ASSERT(inputBufferLength >= sizeof(NET_PACKET_FILTER_TYPES_FLAGS));

    NcmAdapter* ncmAdapter = NcmGetAdapterFromHandle(NetRequestQueueGetAdapter(requestQueue));

    ncmAdapter->m_PacketFilters = *(NET_PACKET_FILTER_TYPES_FLAGS*) inputBuffer;

    NetRequestSetDataComplete(request, STATUS_SUCCESS, sizeof(NET_PACKET_FILTER_TYPES_FLAGS));
}

PAGED
_Use_decl_annotations_
NTSTATUS
NcmAdapter::ConfigNetRequestQueue()
{
    NET_REQUEST_QUEUE_CONFIG queueConfig;

    NET_REQUEST_QUEUE_CONFIG_INIT_DEFAULT_SEQUENTIAL(&queueConfig,
                                                     m_NetAdapter);

    // registers query OIDs with complex behaviors
    for (ULONG i = 0; i < ARRAYSIZE(ComplexQueries); i++)
    {
        NET_REQUEST_QUEUE_CONFIG_ADD_QUERY_DATA_HANDLER(
            &queueConfig,
            ComplexQueries[i].Oid,
            ComplexQueries[i].EvtQueryData,
            ComplexQueries[i].MinimumSize);
    }

    // registers set OIDs with complex behaviors
    for (ULONG i = 0; i < ARRAYSIZE(ComplexSets); i++)
    {
        NET_REQUEST_QUEUE_CONFIG_ADD_SET_DATA_HANDLER(
            &queueConfig,
            ComplexSets[i].Oid,
            ComplexSets[i].EvtSetData,
            ComplexSets[i].MinimumSize);
    }

    m_Stats.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    m_Stats.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
    m_Stats.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;

    //
    // Create the default NETREQUESTQUEUE.
    //
    NCM_RETURN_IF_NOT_NT_SUCCESS_MSG(
        NetRequestQueueCreate(&queueConfig, WDF_NO_OBJECT_ATTRIBUTES, NULL),
        "NetRequestQueueCreate failed");

    return STATUS_SUCCESS;
}
