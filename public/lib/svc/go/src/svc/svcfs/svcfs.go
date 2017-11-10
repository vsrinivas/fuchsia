// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package svcfs implements the Fuchsia service namespace.
package svcfs

import (
	"strings"

	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
)

type Provider func(name string, h zx.Handle)

type Namespace struct {
	Provider   Provider
	Dispatcher *fdio.Dispatcher
}

func (n *Namespace) Serve(h zx.Handle) error {
	if err := n.Dispatcher.AddHandler(h, fdio.ServerHandler(n.handler), 0); err != nil {
		return err
	}

	return h.SignalPeer(0, zx.SignalUser0)
}

func errStatus(err error) zx.Status {
	if err == nil {
		return zx.ErrOk
	}
	if s, ok := err.(zx.Error); ok {
		return s.Status
	}
	return zx.ErrInternal
}

// TODO(abarth): Switch to msg.Pipelined() once that exists.
func pipelined(msg *fdio.Msg) bool {
	return uint32(msg.Arg)&syscall.FsFlagPipeline != 0
}

func (n *Namespace) opClone(msg *fdio.Msg, h zx.Handle) zx.Status {
	err := n.Serve(h)

	if !pipelined(msg) {
		ro := fdio.RioObject{
			RioObjectHeader: fdio.RioObjectHeader{
				Status: errStatus(err),
				Type:   uint32(fdio.ProtocolRemote),
			},
		}
		ro.Write(h, 0)
	}

	if err != nil {
		h.Close()
	}

	return fdio.ErrIndirect.Status
}

func (n *Namespace) handler(msg *fdio.Msg, rh zx.Handle, cookieVal int64) zx.Status {
	op := msg.Op()

	switch op {
	case fdio.OpOpen:
		h := msg.Handle[0]
		if h == zx.HANDLE_INVALID {
			return zx.ErrBadState
		}

		path := string(msg.Data[:msg.Datalen])
		if strings.IndexByte(path, '/') != -1 {
			// TODO(abarth): Implement path traversal.
			h.Close()
			return zx.ErrNotSupported
		}

		if path == "." || path == ".." {
			return n.opClone(msg, h)
		}

		if !pipelined(msg) {
			ro := fdio.RioObject{
				RioObjectHeader: fdio.RioObjectHeader{
					Status: zx.ErrOk,
					Type:   uint32(fdio.ProtocolService),
				},
			}
			ro.Write(h, 0)
		}

		n.Provider(path, h)
		return fdio.ErrIndirect.Status

	case fdio.OpClone:
		h := msg.Handle[0]
		if h == zx.HANDLE_INVALID {
			return zx.ErrBadState
		}

		return n.opClone(msg, h)

	default:
		return zx.ErrNotSupported
	}
}
