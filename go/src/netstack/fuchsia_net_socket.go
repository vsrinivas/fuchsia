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
	spath := socketPath{version: 4, domain: int(d), typ: int(t), protocol: int(p)}
	var s zx.Handle
	if err := ns.dispatcher.opSocket(zx.Handle(0), spath, func(peerS zx.Handle) { s = peerS }); err != nil {
		return zx.Socket(zx.Handle(0)), 0, err
	}
	return zx.Socket(s), 0, nil
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
