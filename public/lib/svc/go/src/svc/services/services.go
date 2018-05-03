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
	"syscall/zx/io"
)

// A Provider holds a zx.Channel that references a directory that contains services.
type Provider struct {
	directory *io.DirectoryInterface
}

// NewProvider returns a new Provider object.
func NewProvider() *Provider {
	return &Provider{}
}

// NewRequest creates a directory request and stores the other end of the channel
// in the Provider object. If another channel was already held in the Provider,
// it will be closed.
func (p *Provider) NewRequest() (zx.Channel, error) {
	req, pxy, err := io.NewDirectoryInterfaceRequest()
	if err != nil {
		return zx.Channel(zx.HandleInvalid), err
	}
	if p.directory != nil && p.directory.Handle().IsValid() {
		p.directory.Close()
	}
	p.directory = pxy
	return (bindings.InterfaceRequest(req)).Channel, nil
}

// Bind stores an io.DirectoryInterface in the Provider. If another channel was already held in
// the Provider, it will be closed.
func (p *Provider) Bind(dir *io.DirectoryInterface) {
	if p.directory != nil && p.directory.Handle().IsValid() {
		p.directory.Close()
	}
	p.directory = dir
}

// Close closes the channel held in the Provider object.
func (p *Provider) Close() {
	if p.directory != nil && p.directory.Handle().IsValid() {
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
