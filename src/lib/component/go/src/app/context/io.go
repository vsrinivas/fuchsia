// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"runtime/pprof"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"unsafe"

	fidlio "fidl/fuchsia/io"
	"fidl/fuchsia/mem"
)

func respond(flags uint32, req fidlio.NodeInterfaceRequest, err error, node fidlio.Node) error {
	if flags&fidlio.OpenFlagDescribe != 0 {
		proxy := fidlio.NodeEventProxy{Channel: req.Channel}
		switch err := err.(type) {
		case nil:
			info, err := node.Describe()
			if err != nil {
				panic(err)
			}
			return proxy.OnOpen(int32(zx.ErrOk), &info)
		case *zx.Error:
			return proxy.OnOpen(int32(err.Status), nil)
		default:
			panic(err)
		}
	}
	return nil
}

type Node interface {
	getIO() fidlio.Node
	addConnection(flags, mode uint32, req fidlio.NodeInterfaceRequest) error
}

type addFn func(fidl.Stub, zx.Channel) error

// TODO(fxb/37419): Remove TransitionalBase after methods landed.
type Service struct {
	*fidlio.NodeTransitionalBase
	Stub  fidl.Stub
	AddFn addFn
}

var _ Node = (*Service)(nil)
var _ fidlio.Node = (*Service)(nil)

func (s *Service) getIO() fidlio.Node {
	return s
}

func (s *Service) addConnection(flags, mode uint32, req fidlio.NodeInterfaceRequest) error {
	// TODO(ZX-3805): this does not implement the node protocol correctly,
	// but matches the behaviour of SDK FVS.
	if flags&fidlio.OpenFlagNodeReference != 0 {
		b := fidl.Binding{
			Stub:    &fidlio.NodeStub{Impl: s},
			Channel: req.Channel,
		}
		return respond(flags, req, b.Init(func(error) {
			if err := b.Close(); err != nil {
				panic(err)
			}
		}), s)
	}
	return respond(flags, req, s.AddFn(s.Stub, req.Channel), s)
}

func (s *Service) Clone(flags uint32, req fidlio.NodeInterfaceRequest) error {
	return s.addConnection(flags, 0, req)
}

func (s *Service) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (s *Service) Describe() (fidlio.NodeInfo, error) {
	var nodeInfo fidlio.NodeInfo
	nodeInfo.SetService(fidlio.Service{})
	return nodeInfo, nil
}

func (s *Service) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (s *Service) GetAttr() (int32, fidlio.NodeAttributes, error) {
	return int32(zx.ErrOk), fidlio.NodeAttributes{
		Mode:      fidlio.ModeTypeService,
		Id:        fidlio.InoUnknown,
		LinkCount: 1,
	}, nil
}

func (s *Service) SetAttr(flags uint32, attributes fidlio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

type Directory interface {
	Get(string) (Node, bool)
	ForEach(func(string, Node))
}

var _ Directory = mapDirectory(nil)

type mapDirectory map[string]Node

func (md mapDirectory) Get(name string) (Node, bool) {
	node, ok := md[name]
	return node, ok
}

func (md mapDirectory) ForEach(fn func(string, Node)) {
	for name, node := range md {
		fn(name, node)
	}
}

var _ Directory = (*pprofDirectory)(nil)

type pprofDirectory struct{}

func (*pprofDirectory) Get(name string) (Node, bool) {
	if p := pprof.Lookup(name); p != nil {
		return &FileWrapper{
			File: &pprofFile{
				p: p,
			},
		}, true
	}
	return nil, false
}

func (*pprofDirectory) ForEach(fn func(string, Node)) {
	for _, p := range pprof.Profiles() {
		fn(p.Name(), &FileWrapper{
			File: &pprofFile{
				p: p,
			},
		})
	}
}

type DirectoryWrapper struct {
	Directory
}

var _ Node = (*DirectoryWrapper)(nil)

func (dir *DirectoryWrapper) GetDirectory() fidlio.Directory {
	return &directoryState{DirectoryWrapper: dir}
}

func (dir *DirectoryWrapper) getIO() fidlio.Node {
	return dir.GetDirectory()
}

func (dir *DirectoryWrapper) addConnection(flags, mode uint32, req fidlio.NodeInterfaceRequest) error {
	ioDir := dir.GetDirectory()
	b := fidl.Binding{
		Stub:    &fidlio.DirectoryStub{Impl: ioDir},
		Channel: req.Channel,
	}
	return respond(flags, req, b.Init(func(error) {
		if err := b.Close(); err != nil {
			panic(err)
		}
	}), ioDir)
}

var _ fidlio.Directory = (*directoryState)(nil)

// TODO(fxb/37419): Remove TransitionalBase after methods landed.
type directoryState struct {
	*fidlio.DirectoryTransitionalBase
	*DirectoryWrapper

	reading bool
	dirents bytes.Buffer
}

func (dirState *directoryState) Clone(flags uint32, req fidlio.NodeInterfaceRequest) error {
	return dirState.addConnection(flags, 0, req)
}

func (dirState *directoryState) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (dirState *directoryState) Describe() (fidlio.NodeInfo, error) {
	var nodeInfo fidlio.NodeInfo
	nodeInfo.SetDirectory(fidlio.DirectoryObject{})
	return nodeInfo, nil
}

func (dirState *directoryState) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) GetAttr() (int32, fidlio.NodeAttributes, error) {
	return int32(zx.ErrOk), fidlio.NodeAttributes{
		Mode:      fidlio.ModeTypeDirectory | uint32(fdio.VtypeIRUSR),
		Id:        fidlio.InoUnknown,
		LinkCount: 1,
	}, nil
}

func (dirState *directoryState) SetAttr(flags uint32, attributes fidlio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

const dot = "."

func (dirState *directoryState) Open(flags, mode uint32, path string, req fidlio.NodeInterfaceRequest) error {
	if path == dot {
		return dirState.addConnection(flags, mode, req)
	}
	const slash = "/"
	if strings.HasSuffix(path, slash) {
		mode |= fidlio.ModeTypeDirectory
		path = path[:len(path)-len(slash)]
	}

	if i := strings.Index(path, slash); i != -1 {
		if node, ok := dirState.Get(path[:i]); ok {
			node := node.getIO()
			if dir, ok := node.(fidlio.Directory); ok {
				return dir.Open(flags, mode, path[i+len(slash):], req)
			}
			return respond(flags, req, &zx.Error{Status: zx.ErrNotDir}, node)
		}
	} else if node, ok := dirState.Get(path); ok {
		return node.addConnection(flags, mode, req)
	}

	return respond(flags, req, &zx.Error{Status: zx.ErrNotFound}, dirState)
}

func (dirState *directoryState) Unlink(path string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) ReadDirents(maxOut uint64) (int32, []uint8, error) {
	if !dirState.reading {
		writeFn := func(name string, node Node) {
			ioNode := node.getIO()
			status, attr, err := ioNode.GetAttr()
			if err != nil {
				panic(err)
			}
			if status := zx.Status(status); status != zx.ErrOk {
				panic(status)
			}
			dirent := syscall.Dirent{
				Ino:  attr.Id,
				Size: uint8(len(name)),
			}
			switch modeType := attr.Mode & fidlio.ModeTypeMask; modeType {
			case fidlio.ModeTypeDirectory:
				dirent.Type = fidlio.DirentTypeDirectory
			case fidlio.ModeTypeFile:
				dirent.Type = fidlio.DirentTypeFile
			case fidlio.ModeTypeService:
				dirent.Type = fidlio.DirentTypeService
			default:
				panic(fmt.Sprintf("unknown mode type: %b", modeType))
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

type File interface {
	GetBytes() []byte
}

var _ File = (*pprofFile)(nil)

type pprofFile struct {
	p *pprof.Profile
}

func (p *pprofFile) GetBytes() []byte {
	var b bytes.Buffer
	if err := p.p.WriteTo(&b, 0); err != nil {
		panic(err)
	}
	return b.Bytes()
}

var _ Node = (*FileWrapper)(nil)

type FileWrapper struct {
	File
}

func (file *FileWrapper) getFile() fidlio.File {
	buf := file.GetBytes()
	fState := fileState{FileWrapper: file}
	fState.Reset(buf)
	return &fState
}

func (file *FileWrapper) getIO() fidlio.Node {
	return file.getFile()
}

func (file *FileWrapper) addConnection(flags, mode uint32, req fidlio.NodeInterfaceRequest) error {
	ioFile := file.getFile()
	b := fidl.Binding{
		Stub:    &fidlio.FileStub{Impl: ioFile},
		Channel: req.Channel,
	}
	return respond(flags, req, b.Init(func(error) {
		if err := b.Close(); err != nil {
			panic(err)
		}
	}), ioFile)
}

var _ fidlio.File = (*fileState)(nil)

// TODO(fxb/37419): Remove TransitionalBase after methods landed.
type fileState struct {
	*fidlio.FileTransitionalBase
	*FileWrapper
	bytes.Reader
}

func (fState *fileState) Clone(flags uint32, req fidlio.NodeInterfaceRequest) error {
	return fState.addConnection(flags, 0, req)
}

func (fState *fileState) Close() (int32, error) {
	return int32(zx.ErrOk), nil
}

func (fState *fileState) Describe() (fidlio.NodeInfo, error) {
	var nodeInfo fidlio.NodeInfo
	nodeInfo.SetFile(fidlio.FileObject{})
	return nodeInfo, nil
}

func (fState *fileState) Sync() (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) GetAttr() (int32, fidlio.NodeAttributes, error) {
	return int32(zx.ErrOk), fidlio.NodeAttributes{
		Mode:        fidlio.ModeTypeFile | uint32(fdio.VtypeIRUSR),
		Id:          fidlio.InoUnknown,
		ContentSize: uint64(fState.Size()),
		LinkCount:   1,
	}, nil
}

func (fState *fileState) SetAttr(flags uint32, attributes fidlio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
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

func (fState *fileState) Seek(offset int64, start fidlio.SeekOrigin) (int32, uint64, error) {
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
