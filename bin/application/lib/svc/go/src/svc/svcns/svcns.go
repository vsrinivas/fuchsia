// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package svcns

import (
	"fmt"

	"application/lib/svc/svcfs"

	"syscall/mx"
	"syscall/mx/mxio/dispatcher"
	"syscall/mx/mxio/rio"
)

type binder interface {
	// Name returns the name of provided fidl service.
	Name() string

	// Create binds an implementation of fidl service to the provided
	// channel and runs it.
	Create(h mx.Handle)
}

type Namespace struct {
	factories  map[string]binder
	Dispatcher *dispatcher.Dispatcher
}

func New() *Namespace {
	return &Namespace{factories: make(map[string]binder)}
}

func (sn *Namespace) ServeDirectory(handle mx.Handle) error {
	d, err := dispatcher.New(rio.Handler)
	if err != nil {
		panic(fmt.Sprintf("context.New: %v", err))
	}

	n := &svcfs.Namespace{
		Provider: func(name string, h mx.Handle) {
			sn.ConnectToService(name, h)
		},
		Dispatcher: d,
	}

	if err := n.Serve(handle); err != nil {
		handle.Close()
		return err
	}

	sn.Dispatcher = d
	return nil
}

func (sn *Namespace) ConnectToService(name string, handle mx.Handle) error {
	factory, ok := sn.factories[name]
	if !ok {
		handle.Close()
		return nil
	}
	factory.Create(handle)
	return nil
}

func (sn *Namespace) AddService(factory binder) {
	sn.factories[factory.Name()] = factory
}
