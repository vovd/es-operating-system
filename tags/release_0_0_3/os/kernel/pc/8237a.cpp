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

#include <stdio.h>
#include <string.h>
#include <es.h>
#include <es/usage.h>
#include <es/clsid.h>
#include <es/handle.h>
#include "io.h"
#include "8237a.h"

const u8 Dmac::pageOffset[4] = { 7, 3, 1, 2 };

Dmac::
Dmac(u8 base, u8 page, int shift) :
    base(base),
    page(page),
    shift(shift)
{
    for (u8 n = 0; n < 4; ++n)
    {
        chan[n].dmac = this;
        chan[n].chan = n;
        chan[n].stop();
    }

    if (shift)  // master?
    {
        // Set channel 0 to the cascade mode.
        setup(0, 0, 0, 0xc0);
        // Enable slave controller
        chan[0].start();
    }
}

void Dmac::
setup(u8 chan, u32 buffer, int len, u8 mode)
{
    ASSERT(buffer < 16 * 1024 * 1024);
    outpb(base + (MODE << shift), mode | chan);
    outpb(page + pageOffset[chan], buffer >> 16);
    outpb(base + (CLEAR_BYTE_POINTER << shift), 0);
    u8 offset = (((chan << 1) + ADDR) << shift);
    outpb(base + offset, buffer >> shift);
    outpb(base + offset, buffer >> (8 + shift));
    offset = (((chan << 1) + COUNT) << shift);
    outpb(base + offset, (len >> shift) - 1);
    outpb(base + offset, ((len >> shift) - 1) >> 8);
}

void Dmac::
Chan::setup(void* buffer, int len, u8 mode)
{
    SpinLock::Synchronized method(dmac->spinLock);

    mode &= IDmac::READ | IDmac::WRITE | IDmac::AUTO_INITIALIZE;
    dmac->setup(chan, ((u32) buffer) & ~0xc0000000, len, mode | 0x40);  // single mode
}

void Dmac::
Chan::start()
{
    SpinLock::Synchronized method(dmac->spinLock);

    outpb(dmac->base + (SINGLE_MASK << dmac->shift), chan);
}

void Dmac::
Chan::stop()
{
    SpinLock::Synchronized method(dmac->spinLock);

    outpb(dmac->base + (SINGLE_MASK << dmac->shift), 0x04 | chan);
}

bool Dmac::
Chan::isDone()
{
    SpinLock::Synchronized method(dmac->spinLock);

    return inpb(dmac->base + (COMMAND << dmac->shift)) & (1 << chan);
}

int Dmac::
Chan::getCount()
{
    SpinLock::Synchronized method(dmac->spinLock);

    outpb(dmac->base + (CLEAR_BYTE_POINTER << dmac->shift), 0);
    int count = inpb(dmac->base + (((chan << 1) + COUNT) << dmac->shift));
    count |= inpb(dmac->base + (((chan << 1) + COUNT) << dmac->shift)) << 8;
    return (count << dmac->shift) + 1;
}

bool Dmac::
Chan::queryInterface(const Guid& riid, void** objectPtr)
{
    if (riid == IID_IDmac)
    {
        *objectPtr = static_cast<IDmac*>(this);
    }
    else if (riid == IID_IInterface)
    {
        *objectPtr = static_cast<IDmac*>(this);
    }
    else
    {
        *objectPtr = NULL;
        return false;
    }
    static_cast<IInterface*>(*objectPtr)->addRef();
    return true;
}

unsigned int Dmac::
Chan::addRef(void)
{
    return dmac->ref.addRef();
}

unsigned int Dmac::
Chan::release(void)
{
    unsigned int count = dmac->ref.release();
    if (count == 0)
    {
        delete dmac;
        return 0;
    }
    return count;
}