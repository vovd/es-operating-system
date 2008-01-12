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

// ISO 9660 File Subsystem

#include <es.h>
#include <es/clsid.h>
#include <es/dateTime.h>
#include <es/endian.h>
#include <es/list.h>
#include <es/ref.h>
#include <es/types.h>
#include <es/utf.h>
#include <es/base/ICache.h>
#include <es/base/IFile.h>
#include <es/base/IStream.h>
#include <es/device/IDiskManagement.h>
#include <es/device/IFileSystem.h>
#include <es/util/IIterator.h>
#include <es/naming/IBinding.h>
#include <es/naming/IContext.h>
#include "iso9660.h"

using namespace es;

class Iso9660FileSystem;
class Iso9660Iterator;
class Iso9660Stream;
class Iso9660StreamUcs2;

class Iso9660Stream : public IStream, public IContext, public IBinding, public IFile
{
    friend class Iso9660FileSystem;
    friend class Iso9660Iterator;
    friend class Iso9660StreamUcs2;

    Iso9660FileSystem*  fileSystem;
    Link<Iso9660Stream> link;

    Ref                 ref;
    ICache*             cache;
    Iso9660Stream*      parent;
    u32                 dirLocation;
    u32                 offset;
    u32                 location;
    u32                 size;
    u8                  flags;
    DateTime            dateTime;

public:
    Iso9660Stream(Iso9660FileSystem* fileSystem, Iso9660Stream* parent, u32 offset, u8* record);
    ~Iso9660Stream();

    bool isRoot();
    int hashCode() const;

    bool findNext(IStream* dir, u8* record);
    virtual Iso9660Stream* lookupPathName(const char*& name);

    // IFile
    unsigned int getAttributes();
    long long getCreationTime();
    long long getLastAccessTime();
    long long getLastWriteTime();
    void setAttributes(unsigned int attributes);
    void setCreationTime(long long time);
    void setLastAccessTime(long long time);
    void setLastWriteTime(long long time);
    bool canRead();
    bool canWrite();
    bool isDirectory();
    bool isFile();
    bool isHidden();
    IStream* getStream();
    IPageable* getPageable();

    // IStream
    long long getPosition();
    void setPosition(long long pos);
    long long getSize();
    void setSize(long long size);
    int read(void* dst, int count);
    int read(void* dst, int count, long long offset);
    int write(const void* src, int count);
    int write(const void* src, int count, long long offset);
    void flush();

    // IBinding
    IInterface* getObject();
    void setObject(IInterface* object);
    int getName(char* name, int len);

    // IContext
    IBinding* bind(const char* name, IInterface* object);
    IContext* createSubcontext(const char* name);
    int destroySubcontext(const char* name);
    IInterface* lookup(const char* name);
    int rename(const char* oldName, const char* newName);
    int unbind(const char* name);
    IIterator* list(const char* name);

    // IInterface
    void* queryInterface(const Guid& riid);
    unsigned int addRef(void);
    unsigned int release(void);
};

class Iso9660StreamUcs2 : public Iso9660Stream
{
    static const u16 dot[2];
    static const u16 dotdot[3];
public:
    Iso9660StreamUcs2(Iso9660FileSystem* fileSystem, Iso9660Stream* parent, u32 offset, u8* record) :
        Iso9660Stream(fileSystem, parent, offset, record)
    {
    }
    Iso9660Stream* lookupPathName(const char*& name);
    int getName(char* name, int len);
};

class Iso9660FileSystem : public IFileSystem
{
    typedef List<Iso9660Stream, &Iso9660Stream::link> Iso9660StreamChain;
    friend class Iso9660Stream;

    Ref                 ref;
    ICacheFactory*      cacheFactory;
    IStream*            disk;
    ICache*             diskCache;
    Iso9660Stream*      root;
    const char*         escapeSequences;
    u8                  rootRecord[256];

    size_t              hashSize;
    Iso9660StreamChain* hashTable;

    u16                 bytsPerSec;

public:
    static const DateTime epoch;
    static const char* ucs2EscapeSequences[3];

    Iso9660FileSystem();
    Iso9660FileSystem(IStream* disk);
    ~Iso9660FileSystem();

    void init();

    Iso9660Stream* lookup(u32 dirLocation, u32 offset);
    void add(Iso9660Stream* stream);
    void remove(Iso9660Stream* stream);

    Iso9660Stream* getRoot();
    Iso9660Stream* createStream(Iso9660FileSystem* fileSystem, Iso9660Stream* parent, u32 offset, u8* record);

    static int hashCode(u32 dirLocation, u32 offset);
    static DateTime getTime(u8* dt);
    static const char* splitPath(const char* path, char* file);
    static const char* splitPath(const char* path, u16* file);
    static bool isDelimitor(int c);

    // IFileSystem
    void mount(IStream* disk);
    void dismount(void);
    void getRoot(IContext** root);
    long long getFreeSpace();
    long long getTotalSpace();
    int checkDisk(bool fixError);
    void format();
    int defrag();

    // IInterface
    void* queryInterface(const Guid& riid);
    unsigned int addRef(void);
    unsigned int release(void);
};

class Iso9660Iterator : public IIterator
{
    Ref             ref;
    Iso9660Stream*  stream;
    long long       ipos;

public:

    Iso9660Iterator(Iso9660Stream* stream);
    ~Iso9660Iterator();

    bool hasNext(void);
    IInterface* next();
    int remove(void);

    void* queryInterface(const Guid& riid);
    unsigned int addRef(void);
    unsigned int release(void);
};
