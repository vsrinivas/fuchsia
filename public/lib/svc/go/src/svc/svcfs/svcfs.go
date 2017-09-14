// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package svcfs implements the Fuchsia service namespace.
package svcfs

import (
	"strings"

	"syscall/mx"
	"syscall/mx/fdio"
)

type Provider func(name string, h mx.Handle)

type Namespace struct {
	Provider   Provider
	Dispatcher *fdio.Dispatcher
}

func (n *Namespace) Serve(h mx.Handle) error {
	if err := n.Dispatcher.AddHandler(h, fdio.ServerHandler(n.handler), 0); err != nil {
		return err
	}

	return h.SignalPeer(0, mx.SignalUser0)
}

func errStatus(err error) mx.Status {
	if err == nil {
		return mx.ErrOk
	}
	if s, ok := err.(mx.Error); ok {
		return s.Status
	}
	return mx.ErrInternal
}

// TODO(abarth): Switch to msg.Pipelined() once that exists.
func pipelined(msg *fdio.Msg) bool {
	return msg.Arg&0x10000000 != 0
}

func (n *Namespace) opClone(msg *fdio.Msg, h mx.Handle) mx.Status {
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

func (n *Namespace) handler(msg *fdio.Msg, rh mx.Handle, cookieVal int64) mx.Status {
	op := msg.Op()

	switch op {
	case fdio.OpOpen:
		h := msg.Handle[0]
		if h == mx.HANDLE_INVALID {
			return mx.ErrBadState
		}

		path := string(msg.Data[:msg.Datalen])
		if strings.IndexByte(path, '/') != -1 {
			// TODO(abarth): Implement path traversal.
			h.Close()
			return mx.ErrNotSupported
		}

		if path == "." || path == ".." {
			return n.opClone(msg, h)
		}

		if !pipelined(msg) {
			ro := fdio.RioObject{
				RioObjectHeader: fdio.RioObjectHeader{
					Status: mx.ErrOk,
					Type:   uint32(fdio.ProtocolService),
				},
			}
			ro.Write(h, 0)
		}

		n.Provider(path, h)
		return fdio.ErrIndirect.Status

	case fdio.OpClone:
		h := msg.Handle[0]
		if h == mx.HANDLE_INVALID {
			return mx.ErrBadState
		}

		return n.opClone(msg, h)

	default:
		return mx.ErrNotSupported
	}
}
