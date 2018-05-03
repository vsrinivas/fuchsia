// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package svcfs implements the Fuchsia service namespace.
package svcfs

import (
	"strings"

	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/io"
)

type Provider func(name string, c zx.Channel)

type Namespace struct {
	Provider Provider
	service io.DirectoryService
}

func (n *Namespace) Clone(flags uint32, req io.ObjectInterfaceRequest) error {
	c := ((fidl.InterfaceRequest)(req)).Channel
	if err := n.Serve(c); err != nil {
		c.Close()
		return err
	}
	if flags&io.KOpenFlagDescribe != 0 {
		pxy := io.ObjectEventProxy(fidl.Proxy{c})
		info := &io.ObjectInfo{ObjectInfoTag: io.ObjectInfoService}
		return pxy.OnOpen(zx.ErrOk, info)
	}
	return nil
}

func (n *Namespace) Close() (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

func (n *Namespace) ListInterfaces() ([]string, error) {
	return nil, nil
}

func (n *Namespace) Bind(iface string) error {
	return nil
}

func (n *Namespace) Describe() (io.ObjectInfo, error) {
	return io.ObjectInfo{ObjectInfoTag: io.ObjectInfoDirectory}, nil
}

func (n *Namespace) Sync() (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

func (n *Namespace) GetAttr() (zx.Status, io.NodeAttributes, error) {
	return zx.ErrNotSupported, io.NodeAttributes{}, nil
}

func (n *Namespace) SetAttr(flags uint32, attributes io.NodeAttributes) (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

func (n *Namespace) Ioctl(opcode uint32,maxOut uint64,handles []zx.Handle,in []uint8) (zx.Status, []zx.Handle, []uint8, error) {
	return zx.ErrNotSupported, nil, nil, nil
}

// Open opens a service and emits an OnOpen event when ready.
func (n *Namespace) Open(flags, _ uint32, path string, obj io.ObjectInterfaceRequest) error {
	respond := func(status zx.Status) error {
		if flags&io.KOpenFlagDescribe != 0 {
			info := &io.ObjectInfo{ObjectInfoTag: io.ObjectInfoService}
			c := fidl.InterfaceRequest(obj).Channel
			pxy := io.ObjectEventProxy(fidl.Proxy{Channel: c})
			return pxy.OnOpen(status, info)
		}
		return nil
	}

	if strings.IndexByte(path, '/') != -1 {
		// TODO(abarth): Implement path traversal.
		ir := fidl.InterfaceRequest(obj)
		ir.Close()
		return respond(zx.ErrNotSupported)
	}

	if path == "." || path == ".." {
		return respond(zx.ErrOk)
	}

	n.Provider(path, ((fidl.InterfaceRequest)(obj)).Channel)
	return respond(zx.ErrOk)
}

// Unlink implements io.Directory for Namespace.
func (n *Namespace) Unlink(path string) (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

// ReadDirents implements io.Directory for Namespace.
func (n *Namespace) ReadDirents(maxOut uint64) (zx.Status, []uint8, error) {
	return zx.ErrNotSupported, nil, nil
}

// Rewind implements io.Directory for Namespace.
func (n *Namespace) Rewind() (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

// GetToken implements io.Directory for Namespace.
func (n *Namespace) GetToken() (zx.Status, zx.Handle, error) {
	// The returned handle is non-nullable, so this will currently cause
	// an internal failure.
	return zx.ErrNotSupported, zx.HandleInvalid, nil
}

// Rename implements io.Directory for Namespace.
func (n *Namespace) Rename(src string, dstParentToken zx.Handle, dst string) (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

// Link implements io.Directory for Namespace.
func (n *Namespace) Link(src string, dstParentToken zx.Handle, dst string) (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

func (n *Namespace) Serve(c zx.Channel) error {
	if _, err := n.service.Add(n, c, nil); err != nil {
		return err
	}
	h := zx.Handle(c)
	return h.SignalPeer(0, zx.SignalUser0)
}
