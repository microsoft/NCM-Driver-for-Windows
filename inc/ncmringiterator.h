// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#include <net/ringcollection.h>
#include <net/virtualaddress.h>

struct NcmOsQueue
{
    NET_RING_COLLECTION const *
        RingCollection;

    NET_EXTENSION
        VirtualAddressExtension;
};

class NcmOsRingIterator
{

protected:

    NcmOsQueue const * const
        m_OsQueue;

    NET_RING * const
        m_Ring;

    UINT32
        m_Index;

    const UINT32
        m_Last;

    inline
    NcmOsRingIterator(
        NcmOsQueue const * const osQueue,
        NET_RING * const ring,
        UINT32 index,
        const UINT32 last
    )
        : m_OsQueue(osQueue)
        , m_Ring(ring)
        , m_Index(index)
        , m_Last(last)
    {
    }

public:

    inline
    UINT32
    GetIndex(
        void
    )
    const
    {
        return m_Index;
    }

    inline
    BOOLEAN
    HasAny(
        void
    )
    const
    {
        return m_Index != m_Last;
    }

    inline
    UINT32
    GetCount(
        void
    )
    const
    {
        return (m_Last - m_Index) & m_Ring->ElementIndexMask;
    }

    inline
    void
    Advance(
        void
    )
    {
        m_Index = NetRingIncrementIndex(m_Ring, m_Index);
    }

    inline
    void
    AdvanceEnd(
        void
    )
    {
        m_Index = m_Last;
    }

};

class NcmFragmentIterator
    : public NcmOsRingIterator
{

public:

    inline
    NcmFragmentIterator(
        NcmOsQueue const * const osQueue,
        UINT32 index,
        const UINT32 last
    )
        : NcmOsRingIterator(osQueue, NetRingCollectionGetFragmentRing(osQueue->RingCollection), index, last)
    {
    }

    inline
    NET_FRAGMENT *
    GetFragment(
        void
    ) const
    {
        return NetRingGetFragmentAtIndex(m_Ring, m_Index);
    }

    inline
    NET_FRAGMENT_VIRTUAL_ADDRESS * const
    GetVirtualAddress(
        void
    ) const
    {
        return NetExtensionGetFragmentVirtualAddress(&m_OsQueue->VirtualAddressExtension, m_Index);
    }

private:

    inline
    void
    Set(
        void
    )
    {
        // packet Iterator's Set() will automatically update both packet ring and fragment ring
        NT_FRE_ASSERT(false);
    }

};

class NcmPacketIterator
    : public NcmOsRingIterator
{

public:

    inline
    NcmPacketIterator(
        NcmOsQueue const * const osQueue,
        UINT32 index,
        const UINT32 last
    )
        : NcmOsRingIterator(osQueue, NetRingCollectionGetPacketRing(osQueue->RingCollection), index, last)
    {
    }

    inline NET_PACKET *
    GetPacket(
        void
    ) const
    {
        return NetRingGetPacketAtIndex(m_Ring, m_Index);
    }

    inline
    NcmFragmentIterator
    GetFragmentIterator(
        void
    ) const
    {
        NET_PACKET const * packet = GetPacket();
        UINT32 const last = NetRingAdvanceIndex(
            NetRingCollectionGetFragmentRing(m_OsQueue->RingCollection),
            packet->FragmentIndex,
            packet->FragmentCount);

        return NcmFragmentIterator(m_OsQueue, packet->FragmentIndex, last);
    }

    inline
    void
    Set(
        void
    )
    {
        //1. update fragment ring begin index according to current index of packet ring
        UINT32 iteratedCount = NetRingGetRangeCount(m_Ring, m_Ring->BeginIndex, m_Index);

        if (iteratedCount > 0)
        {
            UINT32 lastPacketIndex = NetRingAdvanceIndex(m_Ring, m_Ring->BeginIndex, iteratedCount - 1);
            NET_PACKET const * packet = NetRingGetPacketAtIndex(m_Ring, lastPacketIndex);

            NET_RING * const fragmentRing = NetRingCollectionGetFragmentRing(m_OsQueue->RingCollection);

            UINT32 const lastFragmentIndex =
                NetRingAdvanceIndex(fragmentRing, packet->FragmentIndex, packet->FragmentCount);

            fragmentRing->BeginIndex = lastFragmentIndex;
        }

        //2. update packet ring begin index to current index
        m_Ring->BeginIndex = m_Index;
    }
};

inline
NcmPacketIterator
NcmGetAllPackets(
    _In_ NcmOsQueue const * const osQueue
)
{
    NET_RING * ring = NetRingCollectionGetPacketRing(osQueue->RingCollection);

    return NcmPacketIterator(osQueue, ring->BeginIndex, ring->EndIndex);
}

inline
NcmFragmentIterator
NcmGetAllFragments(
    _In_ NcmOsQueue const * const osQueue
)
{
    NET_RING * ring = NetRingCollectionGetFragmentRing(osQueue->RingCollection);

    return NcmFragmentIterator(osQueue, ring->BeginIndex, ring->EndIndex);
}

inline
void
NcmReturnAllPacketsAndFragments(
    _In_ NcmOsQueue const * const osQueue
)
{
    NET_RING * packetRing = NetRingCollectionGetPacketRing(osQueue->RingCollection);

    for (; packetRing->BeginIndex != packetRing->EndIndex;
        packetRing->BeginIndex = NetRingIncrementIndex(packetRing, packetRing->BeginIndex))
    {
        NET_PACKET * packet = (NET_PACKET *)NetRingGetElementAtIndex(packetRing, packetRing->BeginIndex);
        packet->Ignore = 1;
    }

    NET_RING * fragmentRing = NetRingCollectionGetFragmentRing(osQueue->RingCollection);
    fragmentRing->BeginIndex = fragmentRing->EndIndex;
}

inline
SIZE_T
NcmGetPacketDataLength(
    _In_ NcmPacketIterator const * const pi
)
{
    SIZE_T length = 0;

    for (NcmFragmentIterator fi = pi->GetFragmentIterator(); fi.HasAny(); fi.Advance())
    {
        NET_FRAGMENT * fragment = fi.GetFragment();
        length += (SIZE_T) fragment->ValidLength;
    }

    return length;
}

inline
SIZE_T
NcmCopyPacketDataToBuffer(
    _Out_writes_bytes_(BufferLength) PUCHAR BufferDest,
    _In_ NcmPacketIterator const * const pi,
    _In_ SIZE_T BufferLength
)
{
    SIZE_T bytesCopied = 0;

    for (NcmFragmentIterator fi = pi->GetFragmentIterator(); fi.HasAny() && (BufferLength > 0); fi.Advance())
    {
        NET_FRAGMENT const * fragment = fi.GetFragment();
        UCHAR const * pPacketData =
            (UCHAR const *)fi.GetVirtualAddress()->VirtualAddress + fragment->Offset;
        SIZE_T bytesToCopy =
            (BufferLength < (SIZE_T) fragment->ValidLength)
                ? BufferLength
                : (SIZE_T) fragment->ValidLength;
        RtlCopyMemory(BufferDest, pPacketData, bytesToCopy);

        bytesCopied += bytesToCopy;
        BufferDest += bytesToCopy;
        BufferLength -= bytesToCopy;
    }

    return bytesCopied;
}
