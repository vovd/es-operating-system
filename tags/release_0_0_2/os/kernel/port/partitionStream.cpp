/*
 * Copyright (c) 2006
 * Nintendo Co., Ltd.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Nintendo makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

// Disk partition manager. Since this manager can be applied to both
// ATA hard disks and SCSI hard disks, this manager is implemented
// separately from the hard disk device drivers.

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <es.h>
#include <es/handle.h>
#include "partition.h"

using namespace LittleEndian;

//
// PartitionStream
//

PartitionStream::
PartitionStream(PartitionContext* context, int id, u8* entry, u8 entryNo, u32 base, PartitionStream* parent) :
    context(context), id(id), parent(parent), entryNo(entryNo)
{
    IDiskManagement::Geometry geometry;
    getGeometry(&geometry);

    boot = byte(entry + PartitionContext::MBR_BootIndicator);
    system = byte(entry + PartitionContext::MBR_SystemIndicator);
    offset = (long long) geometry.bytesPerSector *
        (base + dword(entry + PartitionContext::MBR_StartingSector));
    size = (long long) geometry.bytesPerSector *
        dword(entry + PartitionContext::MBR_TotalSectors);
    br = (long long) geometry.bytesPerSector * base;

    if (parent)
    {
        // logical partition
        parent->addRef();
        type = PartitionContext::TYPE_LOGICAL;
    }
    else if (system == PartitionContext::PT_EXTENDED ||
             system == PartitionContext::PT_EXTENDED_LBA)
    {
        type = PartitionContext::TYPE_EXTENDED;
    }
    else if (system)
    {
        type = PartitionContext::TYPE_PRIMARY;
    }
    else
    {
        type = PartitionContext::TYPE_EMPTY;
    }

    context->addRef();
}

PartitionStream::
~PartitionStream()
{
    if (type == PartitionContext::TYPE_LOGICAL)
    {
        parent->release();
    }
    ASSERT(ref == 0);
}

bool PartitionStream::
isPrimaryPartition()
{
    return (type == PartitionContext::TYPE_PRIMARY);
}

bool PartitionStream::
isExtendedPartition()
{
    return (type == PartitionContext::TYPE_EXTENDED);
}

bool PartitionStream::
isLogicalPartition()
{
    return (type == PartitionContext::TYPE_LOGICAL);
}

bool PartitionStream::
isRemovable()
{
    int minRef = 1;
    if (type == PartitionContext::TYPE_EXTENDED ||
        type == PartitionContext::TYPE_LOGICAL)
    {
        if (this != context->partitionList.getLast())
        {
            // The minimal reference count for a logical or extended partition
            // except for the last partition is two,
            // because logical partition N depends on logical partition N-1.
            // (The logical partition 0 depends on the extended partition.)
            ++minRef;
        }
    }

    return (ref == minRef);
}

int PartitionStream::
setEntry(u8* entry, Partition* partition, Geometry* geometry, bool secondEntry)
{
    unsigned startingSector;
    unsigned totalSectors;
    unsigned startingCHS;
    unsigned endingCHS;

    startingSector = partition->startingOffset / geometry->bytesPerSector;
    totalSectors = partition->partitionLength / geometry->bytesPerSector;

    if (startingSector == 0 && totalSectors == 0)
    {
        // initialize a partition table entry.
        startingCHS = 0;
        endingCHS = 0;
    }
    else if (context->convertLBAtoCHS(startingSector, totalSectors,
        startingCHS, endingCHS) < 0)
    {
        return -1;
    }

    if (isLogicalPartition())
    {
        if (secondEntry)
        {
            if (startingCHS == PartitionContext::MAX_STARTING_CHS)
            {
                startingCHS = PartitionContext::MAX_STARTING_EPBR_CHS;
            }

            // For the second entry in an EPBR,
            // the origin of a sector is the top of the first EPBR.
            PartitionStream* extended;
            extended = context->lookupPartitionStream(PartitionContext::PREFIX_EXTENDED);
            if (!extended)
            {
                return -1;
            }
            long long origin;
            extended->getOffset(origin);
            startingSector -= (origin / geometry->bytesPerSector);
        }
        else
        {
            // For the first entry in an EPBR,
            // the origin is the top of the EPBR in which the entry exists.
            startingSector -= (br / geometry->bytesPerSector);
        }
    }

    u32 output;
    xbyte(entry + PartitionContext::MBR_BootIndicator, partition->bootIndicator);
    output = (dword(entry + PartitionContext::MBR_StartingCHS) & ~0xffffff) | startingCHS;
    xdword(entry + PartitionContext::MBR_StartingCHS, output);
    xbyte(entry + PartitionContext::MBR_SystemIndicator, partition->partitionType);
    output = (dword(entry + PartitionContext::MBR_EndingCHS) & ~0xffffff) | endingCHS;
    xdword(entry + PartitionContext::MBR_EndingCHS, output);
    xdword(entry + PartitionContext::MBR_TotalSectors, totalSectors);
    xdword(entry + PartitionContext::MBR_StartingSector, startingSector);

    return 0;
}

int PartitionStream::
setParentEPBR(Partition* partition)
{
    if (parent->isLogicalPartition())
    {
        Monitor::Synchronized method(context->monitor);

        Geometry geometry;
        getGeometry(&geometry);

        if (geometry.diskSize <= partition->startingOffset + partition->partitionLength ||
            partition->partitionLength % geometry.bytesPerSector ||
            partition->hiddenSectors != 0)
        {
            return -1;
        }

        if (partition->bootIndicator != PartitionContext::BOOT_FLAG_INACTIVE &&
            partition->bootIndicator != PartitionContext::BOOT_FLAG_ACTIVE)
        {
            return -1;
        }

        // Read EPBR
        u8 epbr[geometry.bytesPerSector];
        context->disk->read(epbr, geometry.bytesPerSector, parent->br);
        if (word(epbr + PartitionContext::MBR_Signature) != PartitionContext::MBR_SIGNATURE)
        {
            esReport("Not found EPBR.\n");
            return -1;
        }

        // Get the second entry in EPBR and set parameters to it.
        u8* entry = context->getEntry(epbr, parent->type, parent->id);
        entry += PartitionContext::MBR_EntrySize;
        if (!entry || setEntry(entry, partition, &geometry, true) < 0)
        {
            return -1;
        }

        // update EPBR.
        context->disk->write(epbr, geometry.bytesPerSector, parent->br);
        context->disk->flush();
    }

    return 0;
}

void PartitionStream::
adjustSize(Geometry* geometry, Partition* partition, long long& partitionSize)
{
    // A partition must end at a cylinder boundary.
    unsigned cylinderSize = geometry->heads *
        geometry->sectorsPerTrack * geometry->bytesPerSector;

    // round down.
    long long endSector;
    endSector = partition->startingOffset + partitionSize;
    endSector -= (endSector % cylinderSize);

    partitionSize = endSector - partition->startingOffset;
}

void PartitionStream::
getOffset(long long& offset)
{
    offset = this->offset;
}

u8 PartitionStream::
getId()
{
    return id;
}

u8 PartitionStream::
getType()
{
    return type;
}

u8* PartitionStream::
getEntry(u8* mbr)
{
    u8* entry = &mbr[PartitionContext::MBR_PartitionTable];
    return entry + entryNo * PartitionContext::MBR_EntrySize;
}

void PartitionStream::
setType(u8 type)
{
    this->type = type;
}

u8 PartitionStream::
getEntryNo()
{
    return entryNo;
}

//
// PartitionStream : getPosition
//

long long PartitionStream::
getPosition()
{
    return 0;
}

void PartitionStream::
setPosition(long long pos)
{
}

long long PartitionStream::
getSize()
{
    return this->size;
}

void PartitionStream::
setSize(long long partitionSize)
{
    Monitor::Synchronized method(context->monitor);

    Partition partition;
    getLayout(&partition);

    partition.partitionLength = partitionSize;
    setLayout(&partition);
}

int PartitionStream::
read(void* dst, int count)
{
    return 0;
}

int PartitionStream::
read(void* dst, int count, long long offset)
{
    if (size < offset + count)
    {
        return -1;
    }
    return context->disk->read(dst, count, this->offset + offset);
}

int PartitionStream::
write(const void* src, int count)
{
    return 0;
}

int PartitionStream::
write(const void* src, int count, long long offset)
{
    if (size < offset + count)
    {
        return -1;
    }

    return context->disk->write(src, count, this->offset + offset);
}

void PartitionStream::
flush()
{
    context->disk->flush();
}

//
// PartitionStream : IDiskManagement
//

int PartitionStream::
initialize()
{
    return -1;
}

int PartitionStream::
getGeometry(Geometry* geometry)
{
    Handle<IDiskManagement> dm(context->disk, true);
    if (dm)
    {
        return dm->getGeometry(geometry);
    }
    return -1;
}

int PartitionStream::
getLayout(Partition* partition)
{
    Monitor::Synchronized method(context->monitor);

    partition->startingOffset = offset;
    partition->partitionLength = size;
    partition->hiddenSectors = 0;
    partition->partitionType = system;
    partition->bootIndicator = boot;
    return 0;
}

int PartitionStream::
setLayout(Partition* partition)
{
    Monitor::Synchronized method(context->monitor);

    // Check paritition parameters.
    Geometry geometry;
    getGeometry(&geometry);
    adjustSize(&geometry, partition, partition->partitionLength);

    if (isExtendedPartition() &&
        (partition->partitionType == PartitionContext::PT_EXTENDED ||
         partition->partitionType == PartitionContext::PT_EXTENDED_LBA))
    {
        // Set the type of the extended partition, depending on the ending sector.
        if (PartitionContext::MAX_CHS <= (partition->startingOffset +
            partition->partitionLength) / geometry.bytesPerSector)
        {
            partition->partitionType = PartitionContext::PT_EXTENDED_LBA;
        }
        else
        {
            partition->partitionType = PartitionContext::PT_EXTENDED;
        }
    }

    if (context->checkPartition(this, &geometry, partition) < 0)
    {
        return -1;
    }

    // Read MBR (or EPBR)
    u8 mbr[geometry.bytesPerSector];
    context->disk->read(mbr, geometry.bytesPerSector, br);
    if (word(mbr + PartitionContext::MBR_Signature) != PartitionContext::MBR_SIGNATURE)
    {
        esReport("Not found MBR.\n");
        return -1;
    }

    u8* entry = context->getEntry(mbr, type, id);
    if (!entry || setEntry(entry, partition, &geometry) < 0)
    {
        return -1;
    }

    if (isLogicalPartition() && parent->isLogicalPartition())
    {
        Partition params = *partition;
        // Update the second entry in the previous EPBR.
        long long offsetEPBR;
        context->getEPBROffset(parent, offsetEPBR);
        params.partitionLength += (params.startingOffset - offsetEPBR);
        params.startingOffset = offsetEPBR;
        params.partitionType = PartitionContext::PT_EXTENDED;
        if (setParentEPBR(&params) < 0)
        {
            return -1;
        }
    }

#ifdef VERBOSE
    // check entry
    esReport("[%02x %06x %02x %06x %08x %08x]\n",
             byte(entry + PartitionContext::MBR_BootIndicator),
             dword(entry + PartitionContext::MBR_StartingCHS) & 0xffffff,
             byte(entry + PartitionContext::MBR_SystemIndicator),
             dword(entry + PartitionContext::MBR_EndingCHS) & 0xffffff,
             dword(entry + PartitionContext::MBR_StartingSector),
             dword(entry + PartitionContext::MBR_TotalSectors));
#endif // VERBOSE

    // Update MBR (or EPBR)
    context->disk->write(mbr, geometry.bytesPerSector, br);
    context->disk->flush();

    // Update PartitionStream.
    boot = byte(entry + PartitionContext::MBR_BootIndicator);
    system = byte(entry + PartitionContext::MBR_SystemIndicator);

    offset = br + (long long) geometry.bytesPerSector *
        dword(entry + PartitionContext::MBR_StartingSector);
    size = (long long) geometry.bytesPerSector *
        dword(entry + PartitionContext::MBR_TotalSectors);

    return 0;
}

//
// PartitionStream : IInterface
//

bool PartitionStream::
queryInterface(const Guid& riid, void** objectPtr)
{
    if (riid == IID_IStream)
    {
        *objectPtr = static_cast<IStream*>(this);
    }
    else if (riid == IID_IDiskManagement)
    {
        *objectPtr = static_cast<IDiskManagement*>(this);
    }
    else if (riid == IID_IInterface)
    {
        *objectPtr = static_cast<IStream*>(this);
    }
    else
    {
        *objectPtr = NULL;
        return false;
    }
    static_cast<IInterface*>(*objectPtr)->addRef();
    return true;
}

unsigned int PartitionStream::
addRef(void)
{
    return ref.addRef();
}

unsigned int PartitionStream::
release(void)
{
    unsigned int count = ref.release();
    if (count == 0)
    {
        context->release();
    }
    return count;
}
