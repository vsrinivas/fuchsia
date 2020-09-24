// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package component

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"log"
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

func respond(ctx fidl.Context, flags uint32, req fidlio.NodeWithCtxInterfaceRequest, err error, node fidlio.NodeWithCtx) error {
	if flags&fidlio.OpenFlagDescribe != 0 {
		proxy := fidlio.NodeEventProxy{Channel: req.Channel}
		switch err := err.(type) {
		case nil:
			info, err := node.Describe(ctx)
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

func logError(err error) {
	log.Print(err)
}

type Node interface {
	getIO() fidlio.NodeWithCtx
	addConnection(ctx fidl.Context, flags, mode uint32, req fidlio.NodeWithCtxInterfaceRequest) error
}

type addFn func(fidl.Context, zx.Channel) error

// TODO(fxbug.dev/37419): Remove TransitionalBase after methods landed.
type Service struct {
	*fidlio.NodeWithCtxTransitionalBase
	AddFn addFn
}

var _ Node = (*Service)(nil)
var _ fidlio.NodeWithCtx = (*Service)(nil)

func (s *Service) getIO() fidlio.NodeWithCtx {
	return s
}

func (s *Service) addConnection(ctx fidl.Context, flags, mode uint32, req fidlio.NodeWithCtxInterfaceRequest) error {
	// TODO(fxbug.dev/33595): this does not implement the node protocol correctly,
	// but matches the behaviour of SDK VFS.
	if flags&fidlio.OpenFlagNodeReference != 0 {
		stub := fidlio.NodeWithCtxStub{Impl: s}
		go ServeExclusive(ctx, &stub, req.Channel, logError)
		return respond(ctx, flags, req, nil, s)
	}
	return respond(ctx, flags, req, s.AddFn(ctx, req.Channel), s)
}

func (s *Service) Clone(ctx fidl.Context, flags uint32, req fidlio.NodeWithCtxInterfaceRequest) error {
	return s.addConnection(ctx, flags, 0, req)
}

func (s *Service) Close(fidl.Context) (int32, error) {
	return int32(zx.ErrOk), nil
}

func (s *Service) Describe(fidl.Context) (fidlio.NodeInfo, error) {
	var nodeInfo fidlio.NodeInfo
	nodeInfo.SetService(fidlio.Service{})
	return nodeInfo, nil
}

func (s *Service) Sync(fidl.Context) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (s *Service) GetAttr(fidl.Context) (int32, fidlio.NodeAttributes, error) {
	return int32(zx.ErrOk), fidlio.NodeAttributes{
		Mode:      fidlio.ModeTypeService,
		Id:        fidlio.InoUnknown,
		LinkCount: 1,
	}, nil
}

func (s *Service) SetAttr(_ fidl.Context, flags uint32, attributes fidlio.NodeAttributes) (int32, error) {
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
	Directory Directory
}

var _ Node = (*DirectoryWrapper)(nil)

func (dir *DirectoryWrapper) GetDirectory() fidlio.DirectoryWithCtx {
	return &directoryState{DirectoryWrapper: dir}
}

func (dir *DirectoryWrapper) getIO() fidlio.NodeWithCtx {
	return dir.GetDirectory()
}

func (dir *DirectoryWrapper) addConnection(ctx fidl.Context, flags, mode uint32, req fidlio.NodeWithCtxInterfaceRequest) error {
	ioDir := dir.GetDirectory()
	stub := fidlio.DirectoryWithCtxStub{Impl: ioDir}
	go ServeExclusive(ctx, &stub, req.Channel, logError)
	return respond(ctx, flags, req, nil, ioDir)
}

var _ fidlio.DirectoryWithCtx = (*directoryState)(nil)

// TODO(fxbug.dev/37419): Remove TransitionalBase after methods landed.
type directoryState struct {
	*fidlio.DirectoryWithCtxTransitionalBase
	*DirectoryWrapper

	reading bool
	dirents bytes.Buffer
}

func (dirState *directoryState) Clone(ctx fidl.Context, flags uint32, req fidlio.NodeWithCtxInterfaceRequest) error {
	return dirState.addConnection(ctx, flags, 0, req)
}

func (dirState *directoryState) Close(fidl.Context) (int32, error) {
	return int32(zx.ErrOk), nil
}

func (dirState *directoryState) Describe(fidl.Context) (fidlio.NodeInfo, error) {
	var nodeInfo fidlio.NodeInfo
	nodeInfo.SetDirectory(fidlio.DirectoryObject{})
	return nodeInfo, nil
}

func (dirState *directoryState) Sync(fidl.Context) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) GetAttr(fidl.Context) (int32, fidlio.NodeAttributes, error) {
	return int32(zx.ErrOk), fidlio.NodeAttributes{
		Mode:      fidlio.ModeTypeDirectory | uint32(fdio.VtypeIRUSR),
		Id:        fidlio.InoUnknown,
		LinkCount: 1,
	}, nil
}

func (dirState *directoryState) SetAttr(_ fidl.Context, flags uint32, attributes fidlio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

const dot = "."

func (dirState *directoryState) Open(ctx fidl.Context, flags, mode uint32, path string, req fidlio.NodeWithCtxInterfaceRequest) error {
	if path == dot {
		return dirState.addConnection(ctx, flags, mode, req)
	}
	const slash = "/"
	if strings.HasSuffix(path, slash) {
		mode |= fidlio.ModeTypeDirectory
		path = path[:len(path)-len(slash)]
	}

	if i := strings.Index(path, slash); i != -1 {
		if node, ok := dirState.Directory.Get(path[:i]); ok {
			node := node.getIO()
			if dir, ok := node.(fidlio.DirectoryWithCtx); ok {
				return dir.Open(ctx, flags, mode, path[i+len(slash):], req)
			}
			return respond(ctx, flags, req, &zx.Error{Status: zx.ErrNotDir}, node)
		}
	} else if node, ok := dirState.Directory.Get(path); ok {
		return node.addConnection(ctx, flags, mode, req)
	}

	return respond(ctx, flags, req, &zx.Error{Status: zx.ErrNotFound}, dirState)
}

func (dirState *directoryState) Unlink(_ fidl.Context, path string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) ReadDirents(ctx fidl.Context, maxOut uint64) (int32, []uint8, error) {
	if !dirState.reading {
		writeFn := func(name string, node Node) {
			ioNode := node.getIO()
			status, attr, err := ioNode.GetAttr(ctx)
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
		dirState.Directory.ForEach(writeFn)
		dirState.reading = true
	} else if dirState.dirents.Len() == 0 {
		status, err := dirState.Rewind(ctx)
		if err != nil {
			panic(err)
		}
		if status := zx.Status(status); status != zx.ErrOk {
			panic(status)
		}
	}
	return int32(zx.ErrOk), dirState.dirents.Next(int(maxOut)), nil
}

func (dirState *directoryState) Rewind(fidl.Context) (int32, error) {
	dirState.reading = false
	dirState.dirents.Reset()
	return int32(zx.ErrOk), nil
}

func (dirState *directoryState) GetToken(fidl.Context) (int32, zx.Handle, error) {
	return int32(zx.ErrNotSupported), zx.HandleInvalid, nil
}

func (dirState *directoryState) Rename(_ fidl.Context, src string, dstParentToken zx.Handle, dst string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) Link(_ fidl.Context, src string, dstParentToken zx.Handle, dst string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) Watch(_ fidl.Context, mask uint32, options uint32, watcher zx.Channel) (int32, error) {
	if err := watcher.Close(); err != nil {
		logError(err)
	}
	return int32(zx.ErrNotSupported), nil
}

type File interface {
	GetReader() (Reader, uint64)
	GetVMO() zx.VMO
}

var _ File = (*pprofFile)(nil)

type pprofFile struct {
	p *pprof.Profile
}

func (p *pprofFile) GetReader() (Reader, uint64) {
	var b bytes.Buffer
	if err := p.p.WriteTo(&b, 0); err != nil {
		panic(err)
	}
	return bytes.NewReader(b.Bytes()), uint64(b.Len())
}

func (*pprofFile) GetVMO() zx.VMO {
	return zx.VMO(zx.HandleInvalid)
}

var _ Node = (*FileWrapper)(nil)

type FileWrapper struct {
	File File
}

func (file *FileWrapper) getFile() fidlio.FileWithCtx {
	reader, size := file.File.GetReader()
	return &fileState{
		FileWrapper: file,
		reader:      reader,
		size:        size,
		vmo:         file.File.GetVMO(),
	}
}

func (file *FileWrapper) getIO() fidlio.NodeWithCtx {
	return file.getFile()
}

func (file *FileWrapper) addConnection(ctx fidl.Context, flags, mode uint32, req fidlio.NodeWithCtxInterfaceRequest) error {
	ioFile := file.getFile()
	stub := fidlio.FileWithCtxStub{Impl: ioFile}
	go ServeExclusive(ctx, &stub, req.Channel, logError)
	return respond(ctx, flags, req, nil, ioFile)
}

var _ fidlio.FileWithCtx = (*fileState)(nil)

type Reader interface {
	io.Reader
	io.ReaderAt
	io.Seeker
}

// TODO(fxbug.dev/37419): Remove TransitionalBase after methods landed.
type fileState struct {
	*fidlio.FileWithCtxTransitionalBase
	*FileWrapper
	reader Reader
	size   uint64
	vmo    zx.VMO
}

func (fState *fileState) Clone(ctx fidl.Context, flags uint32, req fidlio.NodeWithCtxInterfaceRequest) error {
	return fState.addConnection(ctx, flags, 0, req)
}

func (fState *fileState) Close(fidl.Context) (int32, error) {
	return int32(zx.ErrOk), nil
}

func (fState *fileState) Describe(fidl.Context) (fidlio.NodeInfo, error) {
	var nodeInfo fidlio.NodeInfo
	if fState.vmo.Handle().IsValid() {
		h, err := fState.vmo.Handle().Duplicate(zx.RightSameRights)
		if err != nil {
			return nodeInfo, err
		}
		nodeInfo.SetVmofile(fidlio.Vmofile{
			Vmo:    zx.VMO(h),
			Offset: 0,
			Length: fState.size,
		})
	} else {
		nodeInfo.SetFile(fidlio.FileObject{})
	}
	return nodeInfo, nil
}

func (fState *fileState) Sync(fidl.Context) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) GetAttr(fidl.Context) (int32, fidlio.NodeAttributes, error) {
	return int32(zx.ErrOk), fidlio.NodeAttributes{
		Mode:        fidlio.ModeTypeFile | uint32(fdio.VtypeIRUSR),
		Id:          fidlio.InoUnknown,
		ContentSize: fState.size,
		LinkCount:   1,
	}, nil
}

func (fState *fileState) SetAttr(_ fidl.Context, flags uint32, attributes fidlio.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) Read(_ fidl.Context, count uint64) (int32, []uint8, error) {
	if l := fState.size; l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.reader.Read(b)
	if err != nil && err != io.EOF {
		return 0, nil, err
	}
	b = b[:n]
	return int32(zx.ErrOk), b, nil
}

func (fState *fileState) ReadAt(_ fidl.Context, count uint64, offset uint64) (int32, []uint8, error) {
	if l := fState.size - offset; l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.reader.ReadAt(b, int64(offset))
	if err != nil && err != io.EOF {
		return 0, nil, err
	}
	b = b[:n]
	return int32(zx.ErrOk), b, nil
}

func (fState *fileState) Write(_ fidl.Context, data []uint8) (int32, uint64, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (fState *fileState) WriteAt(_ fidl.Context, data []uint8, offset uint64) (int32, uint64, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (fState *fileState) Seek(_ fidl.Context, offset int64, start fidlio.SeekOrigin) (int32, uint64, error) {
	n, err := fState.reader.Seek(offset, int(start))
	return int32(zx.ErrOk), uint64(n), err
}

func (fState *fileState) Truncate(_ fidl.Context, length uint64) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) GetFlags(fidl.Context) (int32, uint32, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (fState *fileState) SetFlags(_ fidl.Context, flags uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (fState *fileState) GetBuffer(_ fidl.Context, flags uint32) (int32, *mem.Buffer, error) {
	return int32(zx.ErrNotSupported), nil, nil
}
