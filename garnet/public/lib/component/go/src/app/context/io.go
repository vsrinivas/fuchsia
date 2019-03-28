// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"bytes"
	"encoding/binary"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/io"
	"unsafe"
)

func respond(flags uint32, req io.NodeInterfaceRequest, err error, node io.Node) error {
	if flags&io.OpenFlagDescribe != 0 {
		proxy := io.NodeEventProxy{Channel: req.Channel}
		switch err := err.(type) {
		case nil:
			info, err := node.Describe()
			if err != nil {
				panic(err)
			}
			return proxy.OnOpen(int32(zx.ErrOk), &info)
		case zx.Error:
			return proxy.OnOpen(int32(err.Status), nil)
		default:
			panic(err)
		}
	}
	return nil
}

type Node interface {
	GetIO() io.Node
	AddConnection(flags, mode uint32, req io.NodeInterfaceRequest) error
}

type ServiceFn func(zx.Channel) error

var _ Node = (ServiceFn)(nil)
var _ io.Node = (ServiceFn)(nil)

func (s ServiceFn) GetIO() io.Node {
	return s
}

func (s ServiceFn) AddConnection(flags, mode uint32, req io.NodeInterfaceRequest) error {
	// TODO(ZX-3805): this does not implement the node protocol correctly,
	// but matches the behaviour of SDK FVS.
	if flags&io.OpenFlagNodeReference != 0 {
		b := fidl.Binding{
			Stub:    &io.NodeStub{Impl: s},
			Channel: req.Channel,
		}
		return respond(flags, req, b.Init(func(error) {
			if err := b.Close(); err != nil {
				panic(err)
			}
		}), s)
	}

	return respond(flags, req, s(req.Channel), s)
}

func (s ServiceFn) Clone(flags uint32, req io.NodeInterfaceRequest) error {
	return s.AddConnection(flags, 0, req)
}

func (s ServiceFn) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (s ServiceFn) Describe() (io.NodeInfo, error) {
	return io.NodeInfo{NodeInfoTag: io.NodeInfoService}, nil
}

func (s ServiceFn) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (s ServiceFn) GetAttr() (int32, io.NodeAttributes, error) {
	return int32(zx.ErrOk), io.NodeAttributes{
		Mode:      io.ModeTypeService,
		LinkCount: 1,
	}, nil
}

func (s ServiceFn) SetAttr(flags uint32, attributes io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (s ServiceFn) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	return int32(zx.ErrNotSupported), nil, nil, nil
}

type Directory interface {
	Get(string) (Node, bool)
	ForEach(func(string, io.Node))
}

var _ Directory = mapDirectory(nil)

type mapDirectory map[string]Node

func (md mapDirectory) Get(name string) (Node, bool) {
	node, ok := md[name]
	return node, ok
}

func (md mapDirectory) ForEach(fn func(string, io.Node)) {
	for name, node := range md {
		fn(name, node.GetIO())
	}
}

type DirectoryWrapper struct {
	Directory
}

var _ Node = (*DirectoryWrapper)(nil)

func (dir *DirectoryWrapper) getIO() io.Directory {
	return &directoryState{DirectoryWrapper: dir}
}

func (dir *DirectoryWrapper) GetIO() io.Node {
	return dir.getIO()
}

func (dir *DirectoryWrapper) addConnection(flags, mode uint32, req io.NodeInterfaceRequest, copy bool) error {
	ioDir := dir.getIO()
	b := fidl.Binding{
		Stub:    &io.DirectoryStub{Impl: ioDir},
		Channel: req.Channel,
	}
	return respond(flags, req, b.Init(func(error) {
		if err := b.Close(); err != nil {
			panic(err)
		}
	}), ioDir)
}

func (dir *DirectoryWrapper) AddConnection(flags, mode uint32, req io.NodeInterfaceRequest) error {
	return dir.addConnection(flags, mode, req, true)
}

var _ io.Directory = (*directoryState)(nil)

type directoryState struct {
	*DirectoryWrapper

	reading bool
	dirents bytes.Buffer
}

func (dirState *directoryState) Clone(flags uint32, req io.NodeInterfaceRequest) error {
	return dirState.AddConnection(flags, 0, req)
}

func (dirState *directoryState) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (dirState *directoryState) Describe() (io.NodeInfo, error) {
	return io.NodeInfo{NodeInfoTag: io.NodeInfoDirectory}, nil
}

func (dirState *directoryState) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) GetAttr() (int32, io.NodeAttributes, error) {
	return int32(zx.ErrOk), io.NodeAttributes{
		Mode:      io.ModeTypeDirectory | uint32(fdio.VtypeIRUSR),
		LinkCount: 1,
	}, nil
}

func (dirState *directoryState) SetAttr(flags uint32, attributes io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	return int32(zx.ErrNotSupported), nil, nil, nil
}

const dot = "."

func (dirState *directoryState) Open(flags, mode uint32, path string, req io.NodeInterfaceRequest) error {
	if path == dot {
		return dirState.AddConnection(flags, mode, req)
	}
	const slash = "/"
	if strings.HasSuffix(path, slash) {
		mode |= io.ModeTypeDirectory
		path = path[:len(path)-len(slash)]
	}

	if i := strings.Index(path, slash); i != -1 {
		if node, ok := dirState.Get(path[:i]); ok {
			node := node.GetIO()
			if dir, ok := node.(io.Directory); ok {
				return dir.Open(flags, mode, path[i+len(slash):], req)
			}
			return respond(flags, req, zx.Error{Status: zx.ErrNotDir}, node)
		}
	} else if node, ok := dirState.Get(path); ok {
		return node.AddConnection(flags, mode, req)
	}

	return respond(flags, req, zx.Error{Status: zx.ErrNotFound}, dirState)
}

func (dirState *directoryState) Unlink(path string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) ReadDirents(maxOut uint64) (int32, []uint8, error) {
	if !dirState.reading {
		writeFn := func(name string, node io.Node) {
			status, attr, err := node.GetAttr()
			if err != nil {
				panic(err)
			}
			if status := zx.Status(status); status != zx.ErrOk {
				panic(status)
			}
			dirent := syscall.Dirent{
				Ino:  attr.Id,
				Size: uint8(len(name)),
				Type: uint8(attr.Mode & io.ModeTypeMask),
			}
			if err := binary.Write(&dirState.dirents, binary.LittleEndian, dirent); err != nil {
				panic(err)
			}
			dirState.dirents.Truncate(dirState.dirents.Len() - int(unsafe.Sizeof(syscall.Dirent{}.Name)))
			if _, err := dirState.dirents.WriteString(name); err != nil {
				panic(err)
			}
		}
		writeFn(dot, dirState)
		dirState.ForEach(writeFn)
		dirState.reading = true
	} else if dirState.dirents.Len() == 0 {
		status, err := dirState.Rewind()
		if err != nil {
			panic(err)
		}
		if status := zx.Status(status); status != zx.ErrOk {
			panic(status)
		}
	}
	return int32(zx.ErrOk), dirState.dirents.Next(int(maxOut)), nil
}

func (dirState *directoryState) Rewind() (int32, error) {
	dirState.reading = false
	dirState.dirents.Reset()
	return int32(zx.ErrOk), nil
}

func (dirState *directoryState) GetToken() (int32, zx.Handle, error) {
	return int32(zx.ErrNotSupported), zx.HandleInvalid, nil
}

func (dirState *directoryState) Rename(src string, dstParentToken zx.Handle, dst string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) Link(src string, dstParentToken zx.Handle, dst string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) Watch(mask uint32, options uint32, watcher zx.Channel) (int32, error) {
	if err := watcher.Close(); err != nil {
		_ = err
	}
	return int32(zx.ErrNotSupported), nil
}
