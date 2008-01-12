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

#ifndef NINTENDO_ES_KERNEL_UART_H_INCLUDED
#define NINTENDO_ES_KERNEL_UART_H_INCLUDED

#include <es/types.h>
#include <es/ref.h>
#include <es/base/IStream.h>
#include <es/base/ICallback.h>
#include <es/ring.h>
#include "thread.h"

class Uart : public IStream, public ICallback
{
    static const u8 IER = 1;
    static const u8 IIR = 2;   // read
    static const u8 FCR = 2;   // write
    static const u8 LCR = 3;
    static const u8 MCR = 4;
    static const u8 LSR = 5;
    static const u8 MSR = 6;

    // IIR
    static const u8 IIR_ID_MASK       = 0x0e;
    static const u8 IIR_MODEM_STATUS  = 0x00;
    static const u8 IIR_THR_EMPTY     = 0x02;
    static const u8 IIR_RECV_DATA     = 0x04;
    static const u8 IIR_RECV_STATUS   = 0x06;
    static const u8 IIR_CHAR_TIMEOUT  = 0x0c;

    Ref         ref;
    int         baseaddr;
    Monitor     monitor;        // for Filled
    Lock        lock;
    u8          buffer[128];
    Ring        ring;
    bool        interrupt;

public:
    Uart(int baseaddr, int bus = 0, int irq = -1);
    ~Uart();

    // Set Baud rate - Divisor Latch Low Byte
    // Default 0x03 = 38,400 BPS
    // 0x01 = 115,200 BPS
    // 0x02 = 56,700 BPS
    // 0x06 = 19,200 BPS
    // 0x0C = 9,600 BPS
    // 0x18 = 4,800 BPS
    // 0x30 = 2,400 BPS
    void setBaud(int rate);

    long long getPosition();
    void setPosition(long long pos);
    long long getSize();
    void setSize(long long size);
    int read(void* dst, int count);
    int read(void* dst, int count, long long offset);
    int write(const void* src, int count);
    int write(const void* src, int count, long long offset);
    void flush();

    int invoke(int);

    bool queryInterface(const Guid& riid, void** objectPtr);
    unsigned int addRef();
    unsigned int release();
};

#endif // NINTENDO_ES_KERNEL_UART_H_INCLUDED
