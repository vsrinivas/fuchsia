// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package component

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	stdio "io"
	"log"
	"runtime"
	"runtime/pprof"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"unsafe"

	"fidl/fuchsia/io"
	"fidl/fuchsia/mem"
)

func respond(ctx fidl.Context, flags uint32, req io.NodeWithCtxInterfaceRequest, err error, node io.NodeWithCtx) error {
	if err != nil {
		defer func() {
			_ = req.Close()
		}()
	}
	if flags&io.OpenFlagDescribe != 0 {
		proxy := io.NodeEventProxy{Channel: req.Channel}
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
	getIO() io.NodeWithCtx
	addConnection(ctx fidl.Context, flags, mode uint32, req io.NodeWithCtxInterfaceRequest) error
}

type Service struct {
	// AddFn is called serially with an incoming request. It must not block, and
	// is expected to handle incoming calls on the request.
	AddFn func(context.Context, zx.Channel) error
}

var _ Node = (*Service)(nil)
var _ io.NodeWithCtx = (*Service)(nil)

func (s *Service) getIO() io.NodeWithCtx {
	return s
}

func (s *Service) addConnection(ctx fidl.Context, flags, mode uint32, req io.NodeWithCtxInterfaceRequest) error {
	// TODO(fxbug.dev/33595): this does not implement the node protocol correctly,
	// but matches the behaviour of SDK VFS.
	if flags&io.OpenFlagNodeReference != 0 {
		stub := io.NodeWithCtxStub{Impl: s}
		go Serve(context.Background(), &stub, req.Channel, ServeOptions{
			OnError: logError,
		})
		return respond(ctx, flags, req, nil, s)
	}
	return respond(ctx, flags, req, s.AddFn(context.Background(), req.Channel), s)
}

func (s *Service) Clone(ctx fidl.Context, flags uint32, req io.NodeWithCtxInterfaceRequest) error {
	return s.addConnection(ctx, flags, 0, req)
}

func (s *Service) Reopen(ctx fidl.Context, options io.ConnectionOptions, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (*Service) CloseDeprecated(fidl.Context) (int32, error) {
	return int32(zx.ErrOk), nil
}

func (*Service) Close(fidl.Context) (io.Node2CloseResult, error) {
	return io.Node2CloseResultWithResponse(io.Node2CloseResponse{}), nil
}

func (*Service) Describe(fidl.Context) (io.NodeInfo, error) {
	var nodeInfo io.NodeInfo
	nodeInfo.SetService(io.Service{})
	return nodeInfo, nil
}

func (*Service) Describe2(_ fidl.Context, query io.ConnectionInfoQuery) (io.ConnectionInfo, error) {
	var connectionInfo io.ConnectionInfo
	if query&io.ConnectionInfoQueryRepresentation != 0 {
		connectionInfo.SetRepresentation(io.RepresentationWithConnector(io.ConnectorInfo{}))
	}
	if query&io.ConnectionInfoQueryRights != 0 {
		// TODO(https://fxbug.dev/77623): Populate the rights requested by the client at connection.
		// This might require separating Service from the VFS implementation so that the latter can
		// hold these rights.
		connectionInfo.SetRights(io.OperationsConnect)
	}
	if query&io.ConnectionInfoQueryAvailableOperations != 0 {
		connectionInfo.SetAvailableOperations(io.OperationsConnect)
	}
	return connectionInfo, nil
}

func (*Service) SyncDeprecated(fidl.Context) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*Service) Sync(fidl.Context) (io.Node2SyncResult, error) {
	return io.Node2SyncResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*Service) GetAttr(fidl.Context) (int32, io.NodeAttributes, error) {
	return int32(zx.ErrOk), io.NodeAttributes{
		Mode:      io.ModeTypeService,
		Id:        io.InoUnknown,
		LinkCount: 1,
	}, nil
}

func (*Service) SetAttr(_ fidl.Context, flags uint32, attributes io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*Service) GetAttributes(fidl.Context, io.NodeAttributesQuery) (io.Node2GetAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2GetAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*Service) UpdateAttributes(fidl.Context, io.NodeAttributes2) (io.Node2UpdateAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2UpdateAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*Service) GetFlags(fidl.Context) (int32, uint32, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*Service) SetFlags(_ fidl.Context, flags uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*Service) QueryFilesystem(_ fidl.Context) (int32, *io.FilesystemInfo, error) {
	return int32(zx.ErrNotSupported), nil, nil
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

func (dir *DirectoryWrapper) GetDirectory() io.DirectoryWithCtx {
	return &directoryState{DirectoryWrapper: dir}
}

func (dir *DirectoryWrapper) getIO() io.NodeWithCtx {
	return dir.GetDirectory()
}

func (dir *DirectoryWrapper) addConnection(ctx fidl.Context, flags, mode uint32, req io.NodeWithCtxInterfaceRequest) error {
	ioDir := dir.GetDirectory()
	stub := io.DirectoryWithCtxStub{Impl: ioDir}
	go Serve(context.Background(), &stub, req.Channel, ServeOptions{
		OnError: logError,
	})
	return respond(ctx, flags, req, nil, ioDir)
}

var _ io.DirectoryWithCtx = (*directoryState)(nil)

type directoryState struct {
	*DirectoryWrapper

	reading bool
	dirents bytes.Buffer
}

func (dirState *directoryState) Clone(ctx fidl.Context, flags uint32, req io.NodeWithCtxInterfaceRequest) error {
	return dirState.addConnection(ctx, flags, 0, req)
}

func (dirState *directoryState) Reopen(ctx fidl.Context, options io.ConnectionOptions, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (*directoryState) CloseDeprecated(fidl.Context) (int32, error) {
	return int32(zx.ErrOk), nil
}

func (*directoryState) Close(fidl.Context) (io.Node2CloseResult, error) {
	return io.Node2CloseResultWithResponse(io.Node2CloseResponse{}), nil
}

func (*directoryState) Describe(fidl.Context) (io.NodeInfo, error) {
	var nodeInfo io.NodeInfo
	nodeInfo.SetDirectory(io.DirectoryObject{})
	return nodeInfo, nil
}

func (*directoryState) Describe2(_ fidl.Context, query io.ConnectionInfoQuery) (io.ConnectionInfo, error) {
	var connectionInfo io.ConnectionInfo
	if query&io.ConnectionInfoQueryRepresentation != 0 {
		connectionInfo.SetRepresentation(io.RepresentationWithDirectory(io.DirectoryInfo{}))
	}
	// TODO(https://fxbug.dev/77623): Populate the rights requested by the client at connection.
	rights := io.RStarDir
	if query&io.ConnectionInfoQueryRights != 0 {
		connectionInfo.SetRights(rights)
	}
	if query&io.ConnectionInfoQueryAvailableOperations != 0 {
		abilities := io.OperationsGetAttributes | io.OperationsEnumerate | io.OperationsTraverse
		connectionInfo.SetAvailableOperations(abilities & rights)
	}
	return connectionInfo, nil
}

func (*directoryState) SyncDeprecated(fidl.Context) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*directoryState) Sync(fidl.Context) (io.Node2SyncResult, error) {
	return io.Node2SyncResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*directoryState) GetAttr(fidl.Context) (int32, io.NodeAttributes, error) {
	return int32(zx.ErrOk), io.NodeAttributes{
		Mode:      io.ModeTypeDirectory | uint32(fdio.VtypeIRUSR),
		Id:        io.InoUnknown,
		LinkCount: 1,
	}, nil
}

func (*directoryState) SetAttr(_ fidl.Context, flags uint32, attributes io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*directoryState) GetAttributes(fidl.Context, io.NodeAttributesQuery) (io.Node2GetAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2GetAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*directoryState) UpdateAttributes(fidl.Context, io.NodeAttributes2) (io.Node2UpdateAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2UpdateAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

const dot = "."

func (dirState *directoryState) Open(ctx fidl.Context, flags, mode uint32, path string, req io.NodeWithCtxInterfaceRequest) error {
	if path == dot {
		return dirState.addConnection(ctx, flags, mode, req)
	}
	const slash = "/"
	if strings.HasSuffix(path, slash) {
		mode |= io.ModeTypeDirectory
		path = path[:len(path)-len(slash)]
	}

	if i := strings.Index(path, slash); i != -1 {
		if node, ok := dirState.Directory.Get(path[:i]); ok {
			node := node.getIO()
			if dir, ok := node.(io.DirectoryWithCtx); ok {
				return dir.Open(ctx, flags, mode, path[i+len(slash):], req)
			}
			return respond(ctx, flags, req, &zx.Error{Status: zx.ErrNotDir}, node)
		}
	} else if node, ok := dirState.Directory.Get(path); ok {
		return node.addConnection(ctx, flags, mode, req)
	}

	return respond(ctx, flags, req, &zx.Error{Status: zx.ErrNotFound}, dirState)
}

func (dirState *directoryState) Open2(ctx fidl.Context, path string, mode io.OpenMode, options io.ConnectionOptions, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (*directoryState) AddInotifyFilter(ctx fidl.Context, path string, filters io.InotifyWatchMask, wd uint32, socket zx.Socket) error {
	return nil
}

func (*directoryState) Unlink(_ fidl.Context, name string, _ io.UnlinkOptions) (io.Directory2UnlinkResult, error) {
	return io.Directory2UnlinkResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (dirState *directoryState) Enumerate(ctx fidl.Context, options io.DirectoryEnumerateOptions, req io.DirectoryIteratorWithCtxInterfaceRequest) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = req.Close()
	return nil
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
			switch modeType := attr.Mode & io.ModeTypeMask; modeType {
			case io.ModeTypeDirectory:
				dirent.Type = io.DirentTypeDirectory
			case io.ModeTypeFile:
				dirent.Type = io.DirentTypeFile
			case io.ModeTypeService:
				dirent.Type = io.DirentTypeService
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

func (*directoryState) GetToken(fidl.Context) (int32, zx.Handle, error) {
	return int32(zx.ErrNotSupported), zx.HandleInvalid, nil
}

func (*directoryState) Rename(_ fidl.Context, src string, dstParentToken zx.Event, dst string) (io.Directory2RenameResult, error) {
	return io.Directory2RenameResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*directoryState) Link(_ fidl.Context, src string, dstParentToken zx.Handle, dst string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*directoryState) Watch(_ fidl.Context, mask uint32, options uint32, watcher zx.Channel) (int32, error) {
	if err := watcher.Close(); err != nil {
		logError(err)
	}
	return int32(zx.ErrNotSupported), nil
}

func (*directoryState) GetFlags(fidl.Context) (int32, uint32, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*directoryState) SetFlags(fidl.Context, uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) AdvisoryLock(fidl.Context, io.AdvisoryLockRequest) (io.AdvisoryLockingAdvisoryLockResult, error) {
	return io.AdvisoryLockingAdvisoryLockResult{}, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", dirState)}
}

func (*directoryState) QueryFilesystem(fidl.Context) (int32, *io.FilesystemInfo, error) {
	return int32(zx.ErrNotSupported), nil, nil
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

var _ File = (*stackTraceFile)(nil)

// stackTraceFile provides a File implementation to expose goroutine
// stacks.
type stackTraceFile struct{}

func (f *stackTraceFile) GetReader() (Reader, uint64) {
	buf := make([]byte, 4096)
	for {
		n := runtime.Stack(buf, true)
		if n < len(buf) {
			return bytes.NewReader(buf[:n]), uint64(n)
		}
		buf = make([]byte, 2*len(buf))
	}
}

func (f *stackTraceFile) GetVMO() zx.VMO {
	return zx.VMO(zx.HandleInvalid)
}

var _ Node = (*FileWrapper)(nil)

type FileWrapper struct {
	File File
}

func (file *FileWrapper) getFile() io.FileWithCtx {
	reader, size := file.File.GetReader()
	return &fileState{
		FileWrapper: file,
		reader:      reader,
		size:        size,
		vmo:         file.File.GetVMO(),
	}
}

func (file *FileWrapper) getIO() io.NodeWithCtx {
	return file.getFile()
}

func (file *FileWrapper) addConnection(ctx fidl.Context, flags, mode uint32, req io.NodeWithCtxInterfaceRequest) error {
	ioFile := file.getFile()
	stub := io.FileWithCtxStub{Impl: ioFile}
	go Serve(context.Background(), &stub, req.Channel, ServeOptions{
		OnError: logError,
	})
	return respond(ctx, flags, req, nil, ioFile)
}

var _ io.FileWithCtx = (*fileState)(nil)

type Reader interface {
	stdio.Reader
	stdio.ReaderAt
	stdio.Seeker
}

type fileState struct {
	*FileWrapper
	reader Reader
	size   uint64
	vmo    zx.VMO
}

func (fState *fileState) Clone(ctx fidl.Context, flags uint32, req io.NodeWithCtxInterfaceRequest) error {
	return fState.addConnection(ctx, flags, 0, req)
}

func (fState *fileState) Reopen(ctx fidl.Context, options io.ConnectionOptions, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (*fileState) CloseDeprecated(fidl.Context) (int32, error) {
	return int32(zx.ErrOk), nil
}

func (fState *fileState) Close(fidl.Context) (io.Node2CloseResult, error) {
	return io.Node2CloseResultWithResponse(io.Node2CloseResponse{}), nil
}

func (fState *fileState) Describe(fidl.Context) (io.NodeInfo, error) {
	var nodeInfo io.NodeInfo
	if fState.vmo.Handle().IsValid() {
		h, err := fState.vmo.Handle().Duplicate(zx.RightSameRights)
		if err != nil {
			return nodeInfo, err
		}
		nodeInfo.SetVmofile(io.Vmofile{
			Vmo:    zx.VMO(h),
			Offset: 0,
			Length: fState.size,
		})
	} else {
		nodeInfo.SetFile(io.FileObject{})
	}
	return nodeInfo, nil
}

func (fState *fileState) Describe2(_ fidl.Context, query io.ConnectionInfoQuery) (io.ConnectionInfo, error) {
	var connectionInfo io.ConnectionInfo
	if query&io.ConnectionInfoQueryRepresentation != 0 {
		var representation io.Representation
		if fState.vmo.Handle().IsValid() {
			// TODO(https://fxbug.dev/77623): The rights on this VMO should be capped at the connection's.
			h, err := fState.vmo.Handle().Duplicate(zx.RightSameRights)
			if err != nil {
				return connectionInfo, err
			}
			var memory io.MemoryInfo
			memory.SetBuffer(mem.Range{
				Vmo:    zx.VMO(h),
				Offset: 0,
				Size:   fState.size,
			})
			representation.SetMemory(memory)
		} else {
			representation.SetFile(io.FileInfo{})
		}

		connectionInfo.SetRepresentation(representation)
	}
	// TODO(https://fxbug.dev/77623): Populate the rights requested by the client at connection.
	rights := io.RStarDir
	if query&io.ConnectionInfoQueryRights != 0 {
		connectionInfo.SetRights(rights)
	}
	if query&io.ConnectionInfoQueryAvailableOperations != 0 {
		abilities := io.OperationsReadBytes | io.OperationsGetAttributes
		connectionInfo.SetAvailableOperations(abilities & rights)
	}
	return connectionInfo, nil
}

func (*fileState) SyncDeprecated(fidl.Context) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*fileState) Sync(fidl.Context) (io.Node2SyncResult, error) {
	return io.Node2SyncResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (fState *fileState) GetAttr(fidl.Context) (int32, io.NodeAttributes, error) {
	return int32(zx.ErrOk), io.NodeAttributes{
		Mode:        io.ModeTypeFile | uint32(fdio.VtypeIRUSR),
		Id:          io.InoUnknown,
		ContentSize: fState.size,
		LinkCount:   1,
	}, nil
}

func (*fileState) SetAttr(_ fidl.Context, flags uint32, attributes io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*fileState) GetAttributes(fidl.Context, io.NodeAttributesQuery) (io.Node2GetAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2GetAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*fileState) UpdateAttributes(fidl.Context, io.NodeAttributes2) (io.Node2UpdateAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2UpdateAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (fState *fileState) read(count uint64) (int32, []uint8, error) {
	if l := fState.size; l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.reader.Read(b)
	if err != nil && err != stdio.EOF {
		return 0, nil, err
	}
	b = b[:n]
	return int32(zx.ErrOk), b, nil
}

func (fState *fileState) ReadDeprecated(_ fidl.Context, count uint64) (int32, []uint8, error) {
	return fState.read(count)
}

func (fState *fileState) Read(_ fidl.Context, count uint64) (io.File2ReadResult, error) {
	s, b, err := fState.read(count)
	if s != int32(zx.ErrOk) {
		return io.File2ReadResultWithErr(s), err
	}
	return io.File2ReadResultWithResponse(io.File2ReadResponse{
		Data: b,
	}), err
}

func (fState *fileState) readAt(count uint64, offset uint64) (int32, []uint8, error) {
	if l := fState.size - offset; l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.reader.ReadAt(b, int64(offset))
	if err != nil && err != stdio.EOF {
		return 0, nil, err
	}
	b = b[:n]
	return int32(zx.ErrOk), b, nil
}

func (fState *fileState) ReadAtDeprecated(_ fidl.Context, count uint64, offset uint64) (int32, []uint8, error) {
	return fState.readAt(count, offset)
}

func (fState *fileState) ReadAt(_ fidl.Context, count uint64, offset uint64) (io.File2ReadAtResult, error) {
	s, b, err := fState.readAt(count, offset)
	if s != int32(zx.ErrOk) {
		return io.File2ReadAtResultWithErr(s), err
	}
	return io.File2ReadAtResultWithResponse(io.File2ReadAtResponse{
		Data: b,
	}), err
}

func (*fileState) WriteDeprecated(_ fidl.Context, data []uint8) (int32, uint64, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*fileState) Write(_ fidl.Context, data []uint8) (io.File2WriteResult, error) {
	return io.File2WriteResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*fileState) WriteAtDeprecated(_ fidl.Context, data []uint8, offset uint64) (int32, uint64, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*fileState) WriteAt(_ fidl.Context, data []uint8, offset uint64) (io.File2WriteAtResult, error) {
	return io.File2WriteAtResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (fState *fileState) SeekDeprecated(_ fidl.Context, offset int64, start io.SeekOrigin) (int32, uint64, error) {
	n, err := fState.reader.Seek(offset, int(start))
	return int32(zx.ErrOk), uint64(n), err
}

func (fState *fileState) Seek(_ fidl.Context, origin io.SeekOrigin, offset int64) (io.File2SeekResult, error) {
	n, err := fState.reader.Seek(offset, int(origin))
	return io.File2SeekResultWithResponse(
		io.File2SeekResponse{
			OffsetFromStart: uint64(n),
		}), err
}

func (*fileState) Truncate(_ fidl.Context, length uint64) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*fileState) Resize(_ fidl.Context, length uint64) (io.File2ResizeResult, error) {
	return io.File2ResizeResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*fileState) GetFlags(fidl.Context) (int32, uint32, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*fileState) SetFlags(_ fidl.Context, flags uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*fileState) GetFlagsDeprecatedUseNode(fidl.Context) (int32, uint32, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*fileState) SetFlagsDeprecatedUseNode(_ fidl.Context, flags uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*fileState) QueryFilesystem(_ fidl.Context) (int32, *io.FilesystemInfo, error) {
	return int32(zx.ErrNotSupported), nil, nil
}

func (fState *fileState) AdvisoryLock(fidl.Context, io.AdvisoryLockRequest) (io.AdvisoryLockingAdvisoryLockResult, error) {
	return io.AdvisoryLockingAdvisoryLockResult{}, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", fState)}
}

func (*fileState) GetBuffer(_ fidl.Context, flags uint32) (int32, *mem.Buffer, error) {
	return int32(zx.ErrNotSupported), nil, nil
}

func (*fileState) GetBackingMemory(_ fidl.Context, flags io.VmoFlags) (io.File2GetBackingMemoryResult, error) {
	return io.File2GetBackingMemoryResultWithErr(int32(zx.ErrNotSupported)), nil
}
