// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package component

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxwait"

	"golang.org/x/sync/errgroup"
)

var (
	// ErrTooManyBytesInResponse indicates too many bytes in a response message.
	ErrTooManyBytesInResponse = errors.New("too many bytes in a response")
	// ErrTooManyHandlesInResponse indicates too many handles in a response message.
	ErrTooManyHandlesInResponse = errors.New("too many handles in a response")
)

// ServeExclusive assumes ownership of req and serially serves requests on it
// via stub. ServeExclusive closes req before returning.
//
// onError is a logging hook that will be called with errors that cannot be propagated.
func ServeExclusive(ctx context.Context, stub fidl.Stub, req zx.Channel, onError func(error)) {
	serveExclusive(nil, ctx, stub, req, onError)
}

// ServeExclusiveConcurrent assumes ownership of req and concurrently serves
// requests on it via stub. ServeExclusiveConcurrent closes req before
// returning.
//
// onError is a logging hook that will be called with errors that cannot be propagated.
func ServeExclusiveConcurrent(ctx context.Context, stub fidl.Stub, req zx.Channel, onError func(error)) {
	g, ctx := errgroup.WithContext(ctx)
	serveExclusive(g, ctx, stub, req, onError)
	if err := g.Wait(); err != nil {
		handleServeError(ctx, err, onError)
	}
}

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

func serveOne(g *errgroup.Group, ctx context.Context, stub fidl.Stub, req zx.Channel, onError func(error)) error {
	bRaw := bytesPool.Get()
	hiRaw := handleInfosPool.Get()

	// Set up deferred cleanup before type-asserting (which can panic).
	var hi []zx.HandleInfo
	cleanup := func() {
		// Arrange for unconsumed handles to close on return.
		for _, hi := range hi {
			if err := hi.Handle.Close(); err != nil {
				onError(fmt.Errorf("failed to close unconsumed inbound handle: %w", err))
			}
		}
		bytesPool.Put(bRaw)
		handleInfosPool.Put(hiRaw)
	}
	defer func() {
		if fn := cleanup; fn != nil {
			fn()
		}
	}()

	b := bRaw.([]byte)
	bOrig := b
	hi = hiRaw.([]zx.HandleInfo)[:0]

	var nb, nhi uint32
	if err := zxwait.WithRetryContext(ctx, func() error {
		var err error
		nb, nhi, err = req.ReadEtc(b, hi[:cap(hi)], 0)
		return err
	}, *req.Handle(), zx.SignalChannelReadable, zx.SignalChannelPeerClosed); err != nil {
		return err
	}
	b = b[:nb]
	hi = hi[:nhi]

	var reqHeader fidl.MessageHeader
	hnb, hnh, err := fidl.UnmarshalWithContext(fidl.NewCtx(), b, hi, &reqHeader)
	if err != nil {
		return err
	}
	if !reqHeader.IsSupportedVersion() {
		return fidl.ErrUnknownMagic
	}
	b = b[hnb:]
	hi = hi[hnh:]

	marshalerCtx := reqHeader.NewCtx()

	// We've done all the synchronous work we can. This closure calls into the
	// stub, which the caller may have requested to happen concurrently.
	//
	// Move deferred cleanup into the closure.
	movedCleanup := cleanup
	cleanup = nil
	dispatch := func() error {
		defer movedCleanup()

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
			hdRaw := handleDispositionsPool.Get()
			defer handleDispositionsPool.Put(hdRaw)

			// Arrange for unconsumed handles to close on return.
			hd := hdRaw.([]zx.HandleDisposition)[:0]
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
			if cnb > cap(b) {
				return fmt.Errorf("slice bounds out of range [:%d] with capacity %d (%w)", cnb, cap(b), ErrTooManyBytesInResponse)
			}
			b = b[:cnb]
			if cnh > cap(hd) {
				return fmt.Errorf("slice bounds out of range [:%d] with capacity %d (%w)", cnh, cap(hd), ErrTooManyHandlesInResponse)
			}
			hd = hd[:cnh]
			if err := req.WriteEtc(b, hd, 0); err != nil {
				return err
			}

			// Consumed, prevent cleanup.
			hd = hd[:0]
		}

		return nil
	}

	// The caller requested concurrent dispatch.
	if g != nil {
		g.Go(dispatch)
		return nil
	}
	return dispatch()
}

func serve(g *errgroup.Group, ctx context.Context, stub fidl.Stub, req zx.Channel, onError func(error)) error {
	for {
		if err := ctx.Err(); err != nil {
			return err
		}
		// Wait for an incoming message before calling serveOne, which acquires
		// pooled memory to read into. This technique avoids O(requests) memory
		// usage, which yields substantial savings when the number of idle requests
		// is high.
		if _, err := zxwait.WaitContext(ctx, *req.Handle(), zx.SignalChannelReadable|zx.SignalChannelPeerClosed); err != nil {
			return err
		}
		if err := serveOne(g, ctx, stub, req, onError); err != nil {
			return err
		}
	}
}

func serveExclusive(g *errgroup.Group, ctx context.Context, stub fidl.Stub, req zx.Channel, onError func(error)) {
	defer func() {
		if err := req.Close(); err != nil {
			onError(fmt.Errorf("failed to close request channel: %w", err))
		}
	}()

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	if err := serve(g, ctx, stub, req, onError); err != nil {
		handleServeError(ctx, err, onError)
	}
}

func handleServeError(ctx context.Context, err error, onError func(error)) {
	if err == ctx.Err() {
		return
	}
	if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrPeerClosed {
		return
	}
	onError(fmt.Errorf("serving terminated: %w", err))
}
