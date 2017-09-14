// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package svcns

import (
	"fmt"

	"svc/svcfs"

	"syscall/mx"
	"syscall/mx/fdio"
)

type binder interface {
	// Name returns the name of provided fidl service.
	Name() string

	// Bind binds an implementation of fidl service to the provided
	// channel and runs it.
	Bind(h mx.Handle)
}

type Namespace struct {
	binders    map[string]binder
	Dispatcher *fdio.Dispatcher
}

func New() *Namespace {
	return &Namespace{binders: make(map[string]binder)}
}

func (sn *Namespace) ServeDirectory(h mx.Handle) error {
	d, err := fdio.NewDispatcher(fdio.Handler)
	if err != nil {
		panic(fmt.Sprintf("context.New: %v", err))
	}

	n := &svcfs.Namespace{
		Provider: func(name string, h mx.Handle) {
			sn.ConnectToService(name, h)
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

func (sn *Namespace) ConnectToService(name string, h mx.Handle) error {
	b, ok := sn.binders[name]
	if !ok {
		h.Close()
		return nil
	}
	b.Bind(h)
	return nil
}

func (sn *Namespace) AddService(b binder) {
	sn.binders[b.Name()] = b
}
