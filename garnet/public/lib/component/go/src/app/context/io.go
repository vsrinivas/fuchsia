// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"bytes"
	"encoding/binary"
	"io"
	"runtime"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	zxio "syscall/zx/io"
	"syscall/zx/mem"
	"unsafe"
)

func respond(flags uint32, req zxio.NodeInterfaceRequest, err error, node zxio.Node) error {
	if flags&zxio.OpenFlagDescribe != 0 {
		proxy := zxio.NodeEventProxy{Channel: req.Channel}
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
	GetIO() zxio.Node
	AddConnection(flags, mode uint32, req zxio.NodeInterfaceRequest) error
}

type ServiceFn func(zx.Channel) error

var _ Node = (*ServiceFn)(nil)
var _ zxio.Node = (*ServiceFn)(nil)

func (s ServiceFn) GetIO() zxio.Node {
	return s
}

func (s ServiceFn) AddConnection(flags, mode uint32, req zxio.NodeInterfaceRequest) error {
	// TODO(ZX-3805): this does not implement the node protocol correctly,
	// but matches the behaviour of SDK FVS.
	if flags&zxio.OpenFlagNodeReference != 0 {
		b := fidl.Binding{
			Stub:    &zxio.NodeStub{Impl: s},
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

func (s ServiceFn) Clone(flags uint32, req zxio.NodeInterfaceRequest) error {
	return s.AddConnection(flags, 0, req)
}

func (s ServiceFn) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (s ServiceFn) Describe() (zxio.NodeInfo, error) {
	return zxio.NodeInfo{NodeInfoTag: zxio.NodeInfoService}, nil
}

func (s ServiceFn) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (s ServiceFn) GetAttr() (int32, zxio.NodeAttributes, error) {
	return int32(zx.ErrOk), zxio.NodeAttributes{
		Mode:      zxio.ModeTypeService,
		LinkCount: 1,
	}, nil
}

func (s ServiceFn) SetAttr(flags uint32, attributes zxio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (s ServiceFn) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	return int32(zx.ErrNotSupported), nil, nil, nil
}

type Directory interface {
	Get(string) (Node, bool)
	ForEach(func(string, zxio.Node))
}

var _ Directory = mapDirectory(nil)

type mapDirectory map[string]Node

func (md mapDirectory) Get(name string) (Node, bool) {
	node, ok := md[name]
	return node, ok
}

func (md mapDirectory) ForEach(fn func(string, zxio.Node)) {
	for name, node := range md {
		fn(name, node.GetIO())
	}
}

type DirectoryWrapper struct {
	Directory
}

var _ Node = (*DirectoryWrapper)(nil)

func (dir *DirectoryWrapper) getIO() zxio.Directory {
	return &directoryState{DirectoryWrapper: dir}
}

func (dir *DirectoryWrapper) GetIO() zxio.Node {
	return dir.getIO()
}

func (dir *DirectoryWrapper) addConnection(flags, mode uint32, req zxio.NodeInterfaceRequest, copy bool) error {
	ioDir := dir.getIO()
	b := fidl.Binding{
		Stub:    &zxio.DirectoryStub{Impl: ioDir},
		Channel: req.Channel,
	}
	return respond(flags, req, b.Init(func(error) {
		if err := b.Close(); err != nil {
			panic(err)
		}
	}), ioDir)
}

func (dir *DirectoryWrapper) AddConnection(flags, mode uint32, req zxio.NodeInterfaceRequest) error {
	return dir.addConnection(flags, mode, req, true)
}

var _ zxio.Directory = (*directoryState)(nil)

type directoryState struct {
	*DirectoryWrapper

	reading bool
	dirents bytes.Buffer
}

func (dirState *directoryState) Clone(flags uint32, req zxio.NodeInterfaceRequest) error {
	return dirState.AddConnection(flags, 0, req)
}

func (dirState *directoryState) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (dirState *directoryState) Describe() (zxio.NodeInfo, error) {
	return zxio.NodeInfo{NodeInfoTag: zxio.NodeInfoDirectory}, nil
}

func (dirState *directoryState) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) GetAttr() (int32, zxio.NodeAttributes, error) {
	return int32(zx.ErrOk), zxio.NodeAttributes{
		Mode:      zxio.ModeTypeDirectory | uint32(fdio.VtypeIRUSR),
		LinkCount: 1,
	}, nil
}

func (dirState *directoryState) SetAttr(flags uint32, attributes zxio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	return int32(zx.ErrNotSupported), nil, nil, nil
}

const dot = "."

func (dirState *directoryState) Open(flags, mode uint32, path string, req zxio.NodeInterfaceRequest) error {
	if path == dot {
		return dirState.AddConnection(flags, mode, req)
	}
	const slash = "/"
	if strings.HasSuffix(path, slash) {
		mode |= zxio.ModeTypeDirectory
		path = path[:len(path)-len(slash)]
	}

	if i := strings.Index(path, slash); i != -1 {
		if node, ok := dirState.Get(path[:i]); ok {
			node := node.GetIO()
			if dir, ok := node.(zxio.Directory); ok {
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
		writeFn := func(name string, node zxio.Node) {
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
				Type: uint8(attr.Mode & zxio.ModeTypeMask),
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

var _ Node = (*goroutineFile)(nil)

type goroutineFile struct{}

func (gf *goroutineFile) getIO() zxio.File {
	buf := make([]byte, 1024)
	for {
		if n := runtime.Stack(buf, true); n < len(buf) {
			buf = buf[:n]
			break
		}
		buf = make([]byte, 2*len(buf))
	}
	fState := fileState{goroutineFile: gf}
	fState.Reset(buf)
	return &fState
}

func (gf *goroutineFile) GetIO() zxio.Node {
	return gf.getIO()
}

func (gf *goroutineFile) AddConnection(flags, mode uint32, req zxio.NodeInterfaceRequest) error {
	ioFile := gf.getIO()
	b := fidl.Binding{
		Stub:    &zxio.FileStub{Impl: ioFile},
		Channel: req.Channel,
	}
	return respond(flags, req, b.Init(func(error) {
		if err := b.Close(); err != nil {
			panic(err)
		}
	}), ioFile)
}

var _ zxio.File = (*fileState)(nil)

type fileState struct {
	*goroutineFile
	bytes.Reader
}

func (fState *fileState) Clone(flags uint32, req zxio.NodeInterfaceRequest) error {
	return fState.AddConnection(flags, 0, req)
}

func (fState *fileState) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (fState *fileState) Describe() (zxio.NodeInfo, error) {
	return zxio.NodeInfo{NodeInfoTag: zxio.NodeInfoFile}, nil
}

func (fState *fileState) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) GetAttr() (int32, zxio.NodeAttributes, error) {
	return int32(zx.ErrOk), zxio.NodeAttributes{
		Mode:        zxio.ModeTypeFile | uint32(fdio.VtypeIRUSR),
		ContentSize: uint64(fState.Size()),
		LinkCount:   1,
	}, nil
}

func (fState *fileState) SetAttr(flags uint32, attributes zxio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	return int32(zx.ErrNotSupported), nil, nil, nil
}

func (fState *fileState) Read(count uint64) (int32, []uint8, error) {
	if l := uint64(fState.Len()); l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.Reader.Read(b)
	if err != nil && err != io.EOF {
		return 0, nil, err
	}
	b = b[:n]
	return int32(zx.ErrOk), b, nil
}

func (fState *fileState) ReadAt(count uint64, offset uint64) (int32, []uint8, error) {
	if l := uint64(fState.Size()) - offset; l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.Reader.ReadAt(b, int64(offset))
	if err != nil && err != io.EOF {
		return 0, nil, err
	}
	b = b[:n]
	return int32(zx.ErrOk), b, nil
}

func (fState *fileState) Write(data []uint8) (int32, uint64, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (fState *fileState) WriteAt(data []uint8, offset uint64) (int32, uint64, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (fState *fileState) Seek(offset int64, start zxio.SeekOrigin) (int32, uint64, error) {
	n, err := fState.Reader.Seek(offset, int(start))
	return int32(zx.ErrOk), uint64(n), err
}

func (fState *fileState) Truncate(length uint64) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) GetFlags() (int32, uint32, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (fState *fileState) SetFlags(flags uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) GetBuffer(flags uint32) (int32, *mem.Buffer, error) {
	return int32(zx.ErrNotSupported), nil, nil
}
