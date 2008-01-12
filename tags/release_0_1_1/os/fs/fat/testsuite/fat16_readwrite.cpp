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

#include <new>
#include <errno.h>
#include <stdlib.h>
#include <es.h>
#include <es/handle.h>
#include <es/exception.h>
#include "vdisk.h"
#include "fatStream.h"

#define TEST(exp)                           \
    (void) ((exp) ||                        \
            (esPanic(__FILE__, __LINE__, "\nFailed test " #exp), 0))

#define  BUF_SIZE  (5 * 1024 * 1024)

static u8 bufR[BUF_SIZE];
static u8 bufW[BUF_SIZE];

static void SetData(u8* buf, long long size)
{
    buf[size-1] = 0;
    while (--size)
    {
        *buf++ = 'a' + size % 26;
    }
}

static long PacketReadWrite(IStream* stream, long long size, long numPacket, bool useOffset)
{
    esReport("size %lld bytes, numPacket %d, useOffset %d\n", size, numPacket, useOffset);

    u8* buf = bufR;
    u8* data = bufW;

    SetData(data, size);
    memset(buf, 0, size);

    long long packetSize;
    if (size % numPacket == 0)
    {
        packetSize = size / numPacket;
    }
    else
    {
        packetSize = size / (numPacket - 1);
    }

    stream->setPosition(0);

    long i;
    long long n;
    long long offset = 0;
    long long pos;
    if (useOffset)
    {
        // check
        // long read(void* dst, long count, long long offset);
        // long write(const void* src, long count, long long offset);
        for (i = 0; i < numPacket; ++i)
        {
            if (size - offset < packetSize)
            {
                packetSize = size - offset;
            }
            n = stream->write(data + offset, packetSize, offset);
            pos = stream->getPosition();
            TEST(pos == 0);
            stream->flush();
            TEST(n == packetSize);
            n = stream->read(buf + offset, packetSize, offset);
            TEST(n == packetSize);
            TEST(memcmp(buf + offset, data + offset, packetSize) == 0);
            offset += packetSize;
        }
        TEST(memcmp(buf, data, size) == 0);
    }
    else
    {
        // check
        // long read(void* dst, long count);
        // long write(const void* src, long count);
        for (i = 0; i < numPacket; ++i)
        {
            if (size - offset < packetSize)
            {
                packetSize = size - offset;
            }
            n = stream->write(data + offset, packetSize);
            stream->flush();
            stream->setPosition(offset);
            pos = stream->getPosition();
            TEST(pos == offset);
            TEST(n == packetSize);
            n = stream->read(buf + offset, packetSize);
            TEST(n == packetSize);
            TEST(memcmp(buf + offset, data + offset, packetSize) == 0);
            offset += packetSize;
        }
        TEST(memcmp(buf, data, size) == 0);
    }

    return 0;
}

static long TestFileSystem(Handle<IContext>  root)
{
    Handle<IFile>       file;

    const char* filename = "test";

    file = root->bind(filename, 0);
    Handle<IStream> stream = file->getStream();

    //                           size         numPacket   useOffset
    TEST(PacketReadWrite(stream, 1024LL,         1,       false) == 0);
    TEST(PacketReadWrite(stream, 1024LL,      1024,       false) == 0);
    TEST(PacketReadWrite(stream, 12 * 1024LL,    3,       false ) == 0);
    TEST(PacketReadWrite(stream, 64 * 1024LL,    4,       false ) == 0);
    TEST(PacketReadWrite(stream, 1024 * 1024LL,  4,       false ) == 0);
    try
    {
        TEST(PacketReadWrite(stream, 5 * 1024 * 1024LL,  4,  false ) == 0);
    }
    catch (SystemException<ENOSPC>& e)
    {

    }

    TEST(PacketReadWrite(stream, 1024LL,         1,       true ) == 0);
    TEST(PacketReadWrite(stream, 1024LL,      1024,       true ) == 0);
    TEST(PacketReadWrite(stream, 12 * 1024LL,    3,       true ) == 0);
    TEST(PacketReadWrite(stream, 64 * 1024LL,    4,       true ) == 0);
    TEST(PacketReadWrite(stream, 1024 * 1024LL,  4,       true ) == 0);
    try
    {
        TEST(PacketReadWrite(stream, 5 * 1024 * 1024LL,  4,  true ) == 0);
    }
    catch (SystemException<ENOSPC>& e)
    {

    }

    return 0;
}

int main(void)
{
    IInterface* ns = 0;
    esInit(&ns);
    Handle<IContext> nameSpace(ns);

    Handle<IClassStore> classStore(nameSpace->lookup("class"));
    esRegisterFatFileSystemClass(classStore);

#ifdef __es__
    Handle<IStream> disk = nameSpace->lookup("device/ata/channel0/device0");
#else
    Handle<IStream> disk = new VDisk(static_cast<char*>("fat16_5MB.img"));
#endif
    long long diskSize;
    diskSize = disk->getSize();
    esReport("diskSize: %lld\n", diskSize);

    Handle<IFileSystem> fatFileSystem;
    long long freeSpace;
    long long totalSpace;

    fatFileSystem = reinterpret_cast<IFileSystem*>(
        esCreateInstance(CLSID_FatFileSystem, IFileSystem::iid()));
    fatFileSystem->mount(disk);
    fatFileSystem->format();
    freeSpace = fatFileSystem->getFreeSpace();
    totalSpace = fatFileSystem->getTotalSpace();
    esReport("Free space %lld, Total space %lld\n", freeSpace, totalSpace);
    {
        Handle<IContext> root;

        root = fatFileSystem->getRoot();
        TestFileSystem(root);
        freeSpace = fatFileSystem->getFreeSpace();
        totalSpace = fatFileSystem->getTotalSpace();
        esReport("Free space %lld, Total space %lld\n", freeSpace, totalSpace);
        esReport("\nChecking the file system...\n");
        TEST(fatFileSystem->checkDisk(false));
    }
    fatFileSystem->dismount();
    fatFileSystem = 0;

    fatFileSystem = reinterpret_cast<IFileSystem*>(
        esCreateInstance(CLSID_FatFileSystem, IFileSystem::iid()));
    fatFileSystem->mount(disk);
    freeSpace = fatFileSystem->getFreeSpace();
    totalSpace = fatFileSystem->getTotalSpace();
    esReport("Free space %lld, Total space %lld\n", freeSpace, totalSpace);
    esReport("\nChecking the file system...\n");
    TEST(fatFileSystem->checkDisk(false));
    fatFileSystem->dismount();
    fatFileSystem = 0;

    esReport("done.\n\n");
}
