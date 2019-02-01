// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package svcns

import (
	"svc/svcfs"
	"syscall/zx"
)

type Binder func(zx.Channel) error

type Namespace struct {
	fs      svcfs.Namespace
	binders map[string]Binder
}

func New() *Namespace {
	ns := Namespace{binders: make(map[string]Binder)}
	ns.fs.Provider = func(name string, c zx.Channel) {
		ns.ConnectToService(name, c)
	}
	return &ns
}

func (sn *Namespace) ServeDirectory(c zx.Channel) error {
	if err := sn.fs.Serve(c); err != nil {
		c.Close()
		return err
	}
	return nil
}

// ConnectToService implements component.ServiceProvider for Namespace.
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
