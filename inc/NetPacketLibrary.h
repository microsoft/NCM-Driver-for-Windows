// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

//
// Following are some helper APIs for common ring manipulations.
// They are all implemented using iterator
//

inline
SIZE_T
GetTxPacketDataLength(
    _In_ NET_RING_PACKET_ITERATOR const * Iterator
)
{
    SIZE_T length = 0;

    for (NET_RING_FRAGMENT_ITERATOR fi = NetRingGetTxPostPacketFragmentIterator(Iterator); 
         NetRingIteratorAny(fi); 
         NetRingAdvanceFragmentIterator(&fi))
    {
        NET_FRAGMENT* fragment = NetRingIteratorGetFragment(&fi);
        length += (SIZE_T) fragment->ValidLength;
    }

    return length;
}

inline
VOID
CompleteTxPacketsBatch(
    _In_ NET_RING_COLLECTION const * Rings,
    _In_ UINT32 BatchSize
)
{
    UINT32 packetCount = 0;

    NET_RING_PACKET_ITERATOR pi = NetRingGetTxDrainPacketIterator(Rings);

    while (NetRingIteratorAny(pi))
    {
        NET_PACKET* packet = NetRingIteratorGetPacket(&pi);

        // this function uses Scratch field as the bit for testing completion
        if (!packet->Scratch)
        {
            break;
        }

        packetCount++;

        NET_RING_FRAGMENT_ITERATOR fi = NetRingGetTxDrainPacketFragmentIterator(&pi);
        NetRingAdvanceEndFragmentIterator(&fi);

        NetRingAdvancePacketIterator(&pi);

        if (packetCount >= BatchSize)
        {
            NetRingSetTxDrainPacketIterator(&pi);
            NetRingSetTxDrainFragmentIterator(&fi);
        }
    }
}

inline
SIZE_T
CopyTxPacketDataToBuffer(
    _Out_writes_bytes_(BufferLength) PUCHAR BufferDest,
    _In_ NET_RING_PACKET_ITERATOR const * Iterator,
    _In_ SIZE_T BufferLength)
{
    SIZE_T bytesCopied = 0;

    for (NET_RING_FRAGMENT_ITERATOR fi = NetRingGetTxPostPacketFragmentIterator(Iterator);
         NetRingIteratorAny(fi) && (BufferLength > 0); 
         NetRingAdvanceFragmentIterator(&fi))
    {
        NET_FRAGMENT * fragment = NetRingIteratorGetFragment(&fi);

        UCHAR const * pPacketData = (UCHAR const *) fragment->VirtualAddress + fragment->Offset;
        SIZE_T bytesToCopy =
            (BufferLength < (SIZE_T) fragment->ValidLength) ? BufferLength : (SIZE_T) fragment->ValidLength;
        RtlCopyMemory(BufferDest, pPacketData, bytesToCopy);

        bytesCopied += bytesToCopy;
        BufferDest += bytesToCopy;
        BufferLength -= bytesToCopy;
    }

    return bytesCopied;
}

inline
void
CancelRxPackets(
    _In_ NET_RING_COLLECTION const * Rings
)
{
    NET_RING_PACKET_ITERATOR pi = NetRingGetAllPacketIterator(Rings);

    for (; NetRingIteratorAny(pi); NetRingAdvancePacketIterator(&pi))
    {
        NetRingIteratorGetPacket(&pi)->Ignore = 1;
    }

    NetRingSetAllPacketIterator(&pi);

    NET_RING_FRAGMENT_ITERATOR fi = NetRingGetAllFragmentIterator(Rings);
    NetRingAdvanceEndFragmentIterator(&fi);
    NetRingSetAllFragmentIterator(&fi);
}
