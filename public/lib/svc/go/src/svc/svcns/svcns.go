// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package svcns

import (
	"fmt"

	"svc/svcfs"

	"syscall/zx"
	"syscall/zx/fdio"
)

type Binder func(zx.Channel) error

type Namespace struct {
	binders    map[string]Binder
	Dispatcher *fdio.Dispatcher
}

func New() *Namespace {
	return &Namespace{binders: make(map[string]Binder)}
}

func (sn *Namespace) ServeDirectory(h zx.Handle) error {
	d, err := fdio.NewDispatcher(fdio.Handler)
	if err != nil {
		panic(fmt.Sprintf("context.New: %v", err))
	}

	n := &svcfs.Namespace{
		Provider: func(name string, h zx.Handle) {
			sn.ConnectToService(name, zx.Channel(h))
		},
		Dispatcher: d,
	}

	if err := n.Serve(h); err != nil {
		h.Close()
		return err
	}

	sn.Dispatcher = d
	return nil
}

func (sn *Namespace) ConnectToService(name string, h zx.Channel) error {
	binder, ok := sn.binders[name]
	if !ok {
		h.Close()
		return nil
	}
	return binder(h)
}

func (sn *Namespace) AddService(n string, b Binder) {
	sn.binders[n] = b
}
