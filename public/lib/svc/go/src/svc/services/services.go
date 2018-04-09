// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package services implements a convenient frontend to a directory that
// contains services.
package services

import (
	"fidl/bindings"

	"syscall/zx"
	"syscall/zx/fdio"
)

// A Provider holds a zx.Channel that references a directory that contains services.
type Provider struct {
	directory zx.Channel
}

// NewProvider returns a new Provider object.
func NewProvider() *Provider {
	return &Provider{}
}

// NewRequest creates a directory request and stores the other end of the channel
// in the Provider object. If another channel was already held in the Provider,
// it will be closed.
func (p *Provider) NewRequest() (zx.Channel, error) {
	c0, c1, err := zx.NewChannel(0)
	if err != nil {
		return zx.Channel(zx.HandleInvalid), err
	}
	if p.directory.Handle().IsValid() {
		p.directory.Close()
	}
	p.directory = c1
	return c0, nil
}

// Bind stores a channel in the Provider. If another channel was already held in
// the Provider, it will be closed.
func (p *Provider) Bind(dir *zx.Channel) {
	if p.directory.Handle().IsValid() {
		p.directory.Close()
	}
	p.directory = *dir
	*dir = zx.Channel(zx.HandleInvalid)
}

// Close closes the channel held in the Provider object.
func (p *Provider) Close() {
	if p.directory.Handle().IsValid() {
		p.directory.Close()
	}
}

// ConnectToServiceAt connects an InterfaceRequest to a service located at path in the
// directory referenced by the Provider.
func (p *Provider) ConnectToServiceAt(c zx.Channel, path string) error {
	return fdio.ServiceConnectAt(*p.directory.Handle(), path, *c.Handle())
}

// ConnectToService connects an InterfaceRequest to a service in the directory referenced
// by the Provider using the interface name as the path.
func (p *Provider) ConnectToService(sr bindings.ServiceRequest) error {
	return p.ConnectToServiceAt(sr.ToChannel(), sr.Name())
}
