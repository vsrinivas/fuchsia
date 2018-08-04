// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"

	"app/context"
	"syscall/zx"

	"fidl/fuchsia/net"
)

type socketProviderImpl struct{}

func (sp *socketProviderImpl) OpenSocket(d net.SocketDomain, t net.SocketType, p net.SocketProtocol) (zx.Socket, int32, error) {
	spath := socketPath{domain: int(d), typ: int(t), protocol: int(p)}
	var s zx.Handle
	if err := ns.socketServer.opSocket(zx.Handle(0), spath, func(peerS zx.Handle) { s = peerS }); err != nil {
		return zx.Socket(zx.Handle(0)), int32(errStatus(err)), nil
	}
	return zx.Socket(s), 0, nil
}

func netStringToString(ns *net.String) (string, net.AddrInfoStatus) {
	v := ns.Val[:]
	if len(v) < int(ns.Len) {
		return "", net.AddrInfoStatusBufferOverflow
	}
	return string(v[:ns.Len]), net.AddrInfoStatusOk
}

func (sp *socketProviderImpl) GetAddrInfo(n *net.String, s *net.String, h *net.AddrInfoHints) (net.AddrInfoStatus, int32, *net.AddrInfo, *net.AddrInfo, *net.AddrInfo, *net.AddrInfo, error) {
	var node *string
	if n != nil {
		str, status := netStringToString(n)
		if status != net.AddrInfoStatusOk {
			return status, 0, nil, nil, nil, nil, nil
		}
		node = &str
	}
	var service *string
	if s != nil {
		str, status := netStringToString(s)
		if status != net.AddrInfoStatusOk {
			return status, 0, nil, nil, nil, nil, nil
		}
		service = &str
	}

	status, addrInfo := ns.socketServer.GetAddrInfo(node, service, h)
	if status != 0 {
		return status, 0, nil, nil, nil, nil, nil
	}

	nai := len(addrInfo)
	if nai > 4 {
		nai = 4
	}
	ai := make([]*net.AddrInfo, 4)
	for i := 0; i < 4; i++ {
		if i < nai {
			ai[i] = addrInfo[i]
		} else {
			ai[i] = nil
		}
	}
	return 0, int32(nai), ai[0], ai[1], ai[2], ai[3], nil
}

var socketProvider *net.LegacySocketProviderService

// AddLegacySocketProvider registers the legacy socket provider with the
// application context, allowing it to respond to FIDL queries.
func AddLegacySocketProvider(ctx *context.Context) error {
	if socketProvider != nil {
		return fmt.Errorf("AddLegacySocketProvider must be called only once")
	}
	socketProvider = &net.LegacySocketProviderService{}
	ctx.OutgoingService.AddService(net.LegacySocketProviderName, func(c zx.Channel) error {
		_, err := socketProvider.Add(&socketProviderImpl{}, c, nil)
		return err
	})

	return nil
}
