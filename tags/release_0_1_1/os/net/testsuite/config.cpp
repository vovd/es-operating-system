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

#include <string.h>
#include <es.h>
#include <es/endian.h>
#include <es/handle.h>
#include <es/list.h>
#include <es/base/IStream.h>
#include <es/device/INetworkInterface.h>
#include <es/naming/IContext.h>
#include <es/net/ISocket.h>
#include <es/net/IInternetConfig.h>
#include <es/net/IResolver.h>
#include <es/net/arp.h>

#define TEST(exp)                           \
    (void) ((exp) ||                        \
            (esPanic(__FILE__, __LINE__, "\nFailed test " #exp), 0))

using namespace es;

extern int esInit(IInterface** nameSpace);
extern void esRegisterInternetProtocol(IContext* context);

int main()
{
    IInterface* root = NULL;
    esInit(&root);
    Handle<IContext> context(root);

    esRegisterInternetProtocol(context);

    // Create resolver object
    Handle<IResolver> resolver = context->lookup("network/resolver");

    // Create internet config object
    Handle<IInternetConfig> config = context->lookup("network/config");

    // Test local ping
    Handle<IInternetAddress> loopback = resolver->getHostByAddress(&InAddrLoopback.addr, sizeof InAddrLoopback, 1);
    loopback->isReachable(10000000);

    // Test bind and connect operations
    Handle<ISocket> socket = loopback->socket(AF_INET, ISocket::Datagram, 53);
    socket->connect(loopback, 53);

    // Test read and write operations
    char output[4] = "xyz";
    socket->write(output, 4);

    char input[4];
    IInternetAddress* remoteAddress;
    int remotePort;
    socket->recvFrom(input, 4, 0, &remoteAddress, &remotePort);
    esReport("'%s', %d\n", input, remotePort);
    ASSERT(remoteAddress->isLoopback());
    if (remoteAddress)
    {
        remoteAddress->release();
        remoteAddress = 0;
    }

    // Test close operation
    TEST(!socket->isClosed());
    socket->close();
    // TEST(socket->isClosed());    // XXX This test doesn't work now...

    // Setup DIX interface
    Handle<INetworkInterface> nic = context->lookup("device/ethernet");
    nic->start();
    int dixID = config->addInterface(nic);
    esReport("dixID: %d\n", dixID);

    ASSERT(config->getScopeID(nic) == dixID);

    // Register host address (192.168.2.40)
    InAddr addr = { htonl(192 << 24 | 168 << 16 | 2 << 8 | 40) };
    Handle<IInternetAddress> host = resolver->getHostByAddress(&addr.addr, sizeof addr, dixID);
    config->addAddress(host, 16);
    esSleep(90000000);  // Wait for the host address to be settled.

    // Register a default router (192.168.2.1)
    InAddr addrRouter = { htonl(192 << 24 | 168 << 16 | 2 << 8 | 1) };
    Handle<IInternetAddress> router = resolver->getHostByAddress(&addrRouter.addr, sizeof addr, dixID);
    config->addRouter(router);

    // Test remote ping
    for (int i = 1; i <= 3; ++i)
    {
        bool reachable = router->isReachable(10000000);
        esReport("ping #%d: %d\n", i, reachable);
    }

    esSleep(10000000);

    config->removeRouter(router);
    config->removeAddress(host);

    nic->stop();

    esReport("done.\n");
}