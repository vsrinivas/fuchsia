// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package component

import (
	"fmt"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxwait"
)

var bytesPool = sync.Pool{
	New: func() interface{} {
		return make([]byte, zx.ChannelMaxMessageBytes)
	},
}

var handleInfosPool = sync.Pool{
	New: func() interface{} {
		return make([]zx.HandleInfo, zx.ChannelMaxMessageHandles)
	},
}

var handleDispositionsPool = sync.Pool{
	New: func() interface{} {
		return make([]zx.HandleDisposition, zx.ChannelMaxMessageHandles)
	},
}

func serveOne(ctx fidl.Context, stub fidl.Stub, req zx.Channel, onError func(error)) error {
	b := bytesPool.Get().([]byte)
	defer bytesPool.Put(b)

	bOrig := b

	hi := handleInfosPool.Get().([]zx.HandleInfo)
	defer handleInfosPool.Put(hi)

	// Arrange for unconsumed handles to close on return.
	hi = hi[:0]
	defer func() {
		for _, hi := range hi {
			if err := hi.Handle.Close(); err != nil {
				onError(fmt.Errorf("failed to close unconsumed inbound handle: %w", err))
			}
		}
	}()

	var nb, nhi uint32
	if err := zxwait.WithRetry(func() error {
		var err error
		nb, nhi, err = req.ReadEtc(b, hi[:cap(hi)], 0)
		return err
	}, *req.Handle(), zx.SignalChannelReadable, zx.SignalChannelPeerClosed); err != nil {
		return err
	}
	b = b[:nb]
	hi = hi[:nhi]

	var reqHeader fidl.MessageHeader
	hnb, hnh, err := fidl.Unmarshal(b, hi, &reqHeader)
	if err != nil {
		return err
	}
	if !reqHeader.IsSupportedVersion() {
		return fidl.ErrUnknownMagic
	}
	b = b[hnb:]
	hi = hi[hnh:]

	marshalerCtx := reqHeader.NewCtx()
	p, shouldRespond, err := stub.Dispatch(fidl.DispatchArgs{
		Ctx:         fidl.WithMarshalerContext(ctx, marshalerCtx),
		Ordinal:     reqHeader.Ordinal,
		Bytes:       b,
		HandleInfos: hi,
	})
	if err != nil {
		return err
	}

	// Consumed, prevent cleanup.
	hi = hi[:0]

	if shouldRespond {
		hd := handleDispositionsPool.Get().([]zx.HandleDisposition)
		defer handleDispositionsPool.Put(hd)

		// Arrange for unconsumed handles to close on return.
		hd = hd[:0]
		defer func() {
			for _, hd := range hd {
				if err := hd.Handle.Close(); err != nil {
					onError(fmt.Errorf("failed to close unconsumed outbound handle: %w", err))
				}
			}
		}()

		b = bOrig

		respHeader := marshalerCtx.NewHeader()
		respHeader.Ordinal = reqHeader.Ordinal
		respHeader.Txid = reqHeader.Txid
		cnb, cnh, err := fidl.MarshalHeaderThenMessage(&respHeader, p, b, hd[:cap(hd)])
		if err != nil {
			return err
		}
		b = b[:cnb]
		hd = hd[:cnh]
		if err := req.WriteEtc(b, hd, 0); err != nil {
			return err
		}

		// Consumed, prevent cleanup.
		hd = hd[:0]
	}

	return nil
}

func serve(ctx fidl.Context, stub fidl.Stub, req zx.Channel, onError func(error)) error {
	for {
		if err := ctx.Err(); err != nil {
			return err
		}
		// Wait for an incoming message before calling serveOne, which acquires
		// pooled memory to read into. This technique avoids O(requests) memory
		// usage, which yields substantial savings when the number of idle requests
		// is high.
		if _, err := zxwait.Wait(*req.Handle(), zx.SignalChannelReadable|zx.SignalChannelPeerClosed, zx.TimensecInfinite); err != nil {
			return err
		}
		if err := serveOne(ctx, stub, req, onError); err != nil {
			return err
		}
	}
}

// ServeExclusive assumes ownership of req and serially serves requests on it
// via stub until ctx is called or req's peer is closed. ServeExclusive closes
// req before returning.
func ServeExclusive(ctx fidl.Context, stub fidl.Stub, req zx.Channel, onError func(error)) {
	defer func() {
		if err := req.Close(); err != nil {
			onError(fmt.Errorf("failed to close request channel: %w", err))
		}
	}()
	if err := serve(ctx, stub, req, onError); err != nil {
		if err == ctx.Err() {
			return
		}
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrPeerClosed {
			return
		}
		onError(fmt.Errorf("serving terminated: %w", err))
	}
}
