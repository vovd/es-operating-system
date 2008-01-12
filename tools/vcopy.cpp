/*
 * Copyright (c) 2006, 2007
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
#include <stdio.h>
#include <stdlib.h>
#include <es.h>
#include <es/clsid.h>
#include <es/exception.h>
#include <es/handle.h>
#include <es/base/IClassStore.h>
#include <es/base/IStream.h>
#include <es/base/IFile.h>
#include <es/device/IFileSystem.h>
#include <es/naming/IContext.h>
#include <es/handle.h>
#include "vdisk.h"

u8 sec[512];

static IContext* checkDestinationPath(char* dst, Handle<IContext> root)
{
    Handle<IContext> currentDir = root;
    char buf[1024];
    ASSERT(strlen(dst)+1 <= sizeof(buf));
    memmove(buf, dst, strlen(dst)+1);

    char* path = buf;
    char* end = &buf[strlen(dst)];
    char* next;
    while (path < end)
    {
        next = strchr(path, '/');
        if (next)
        {
            *next = 0;
        }
        else
        {
            Handle<IFile> last = currentDir->lookup(path);
            if (!last || last->isFile())
            {
                return 0; // This indicates destination filename is specified.
            }
            currentDir = last;
            break;
        }

        Handle<IInterface> interface = currentDir->lookup(path);
        if (interface)
        {
            Handle<IFile> file = interface;
            if (file->isFile())
            {
                esReport("Error: invalid path. %s is a file.\n", path);
                esThrow(EINVAL);
                // NOT REACHED HERE
            }
            currentDir = interface;
        }
        else
        {
            Handle<IContext> dir = interface;
            currentDir = currentDir->createSubcontext(path);
            ASSERT(currentDir);
        }
        path = next + 1;
    }

    return currentDir; // This indicates destination directory is specified.
}

static const char* getFilename(const char* p)
{
    const char* head = p;
    p += strlen(p);
    while (head < --p)
    {
        if (strchr(":/\\", *p))
        {
            ++p;
            break;
        }
    }
    return p;
}

void copy(Handle<IContext> root, char* filename, char* dst)
{
    FILE* f = fopen(filename, "rb");
    if (f == 0)
    {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(EXIT_FAILURE);
    }

    const char* p;
    Handle<IContext> currentDir = checkDestinationPath(dst, root);
    if (currentDir)
    {
        p = getFilename(filename);
    }
    else
    {
        p = dst;
        currentDir = root;
    }

    Handle<IFile> file(currentDir->bind(p, 0));
    if (!file)
    {
        file = currentDir->lookup(p);
    }
    Handle<IStream> stream(file->getStream());
    stream->setSize(0);
    for (;;)
    {
        long n = fread(sec, 1, 512, f);
        if (n < 0)
        {
            exit(EXIT_FAILURE);
        }
        if (n == 0)
        {
            return;
        }
        do {
            long len = stream->write(sec, n);
            if (len <= 0)
            {
                exit(EXIT_FAILURE);
            }
            n -= len;
        } while (0 < n);
    }
}

int main(int argc, char* argv[])
{
    IInterface* ns = 0;
    esInit(&ns);
    Handle<IContext> nameSpace(ns);

    Handle<IClassStore> classStore(nameSpace->lookup("class"));
    esRegisterFatFileSystemClass(classStore);

    if (argc < 3)
    {
        esReport("usage: %s disk_image file [destination_path]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Handle<IStream> disk = new VDisk(static_cast<char*>(argv[1]));
    Handle<IFileSystem> fatFileSystem;
    fatFileSystem = reinterpret_cast<IFileSystem*>(
        esCreateInstance(CLSID_FatFileSystem, IFileSystem::iid()));
    fatFileSystem->mount(disk);

    long long freeSpace;
    long long totalSpace;
    freeSpace = fatFileSystem->getFreeSpace();
    totalSpace = fatFileSystem->getTotalSpace();
    esReport("Free space %lld, Total space %lld\n", freeSpace, totalSpace);

    {
        Handle<IContext> root;
        root = fatFileSystem->getRoot();
        char* dst = (char*) (3 < argc ? argv[3] : "");
        copy(root, argv[2], dst);
    }
    fatFileSystem->dismount();
    fatFileSystem = 0;
}
