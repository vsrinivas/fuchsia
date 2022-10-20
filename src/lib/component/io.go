// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

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
	"fidl/fuchsia/unknown"
)

func respond(ctx fidl.Context, flags io.OpenFlags, req io.NodeWithCtxInterfaceRequest, err error, node Node) error {
	if err != nil {
		defer func() {
			_ = req.Close()
		}()
	}
	if flags&io.OpenFlagsDescribe != 0 {
		proxy := io.NodeEventProxy{Channel: req.Channel}
		switch err := err.(type) {
		case nil:
			info, err := node.DescribeDeprecated(ctx)
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
	getIO() (io.NodeWithCtx, func() error, error)
	addConnection(ctx fidl.Context, flags io.OpenFlags, mode uint32, req io.NodeWithCtxInterfaceRequest) error
	DescribeDeprecated(fidl.Context) (io.NodeInfoDeprecated, error)
}

func noop() error {
	return nil
}

type Service struct {
	// AddFn is called serially with an incoming request. It must not block, and
	// is expected to handle incoming calls on the request.
	AddFn func(context.Context, zx.Channel) error
}

var _ Node = (*Service)(nil)
var _ io.NodeWithCtx = (*Service)(nil)

func (s *Service) getIO() (io.NodeWithCtx, func() error, error) {
	return s, noop, nil
}

func (s *Service) addConnection(ctx fidl.Context, flags io.OpenFlags, mode uint32, req io.NodeWithCtxInterfaceRequest) error {
	// TODO(fxbug.dev/33595): this does not implement the node protocol correctly,
	// but matches the behaviour of SDK VFS.
	if flags&io.OpenFlagsNodeReference != 0 {
		stub := io.NodeWithCtxStub{Impl: s}
		go Serve(context.Background(), &stub, req.Channel, ServeOptions{
			OnError: logError,
		})
		return respond(ctx, flags, req, nil, s)
	}
	return respond(ctx, flags, req, s.AddFn(context.Background(), req.Channel), s)
}

func (s *Service) Clone(ctx fidl.Context, flags io.OpenFlags, req io.NodeWithCtxInterfaceRequest) error {
	return s.addConnection(ctx, flags, 0, req)
}

func (s *Service) Reopen(ctx fidl.Context, rights *io.RightsRequest, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (*Service) Close(fidl.Context) (unknown.CloseableCloseResult, error) {
	return unknown.CloseableCloseResultWithResponse(unknown.CloseableCloseResponse{}), nil
}

func (*Service) DescribeDeprecated(fidl.Context) (io.NodeInfoDeprecated, error) {
	var nodeInfo io.NodeInfoDeprecated
	nodeInfo.SetService(io.Service{})
	return nodeInfo, nil
}

func (*Service) GetConnectionInfo(fidl.Context) (io.ConnectionInfo, error) {
	var connectionInfo io.ConnectionInfo
	// TODO(https://fxbug.dev/77623): Populate the rights requested by the client at connection.
	// This might require separating Service from the VFS implementation so that the latter can
	// hold these rights.
	connectionInfo.SetRights(io.OperationsConnect)
	return connectionInfo, nil
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

func (*Service) SetAttr(fidl.Context, io.NodeAttributeFlags, io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*Service) GetAttributes(fidl.Context, io.NodeAttributesQuery) (io.Node2GetAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2GetAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*Service) UpdateAttributes(fidl.Context, io.MutableNodeAttributes) (io.Node2UpdateAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2UpdateAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*Service) GetFlags(fidl.Context) (int32, io.OpenFlags, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*Service) SetFlags(fidl.Context, io.OpenFlags) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*Service) QueryFilesystem(fidl.Context) (int32, *io.FilesystemInfo, error) {
	return int32(zx.ErrNotSupported), nil, nil
}

func (*Service) Query(fidl.Context) ([]uint8, error) {
	return []byte(io.NodeProtocolName), nil
}

type Directory interface {
	Get(string) (Node, bool)
	ForEach(func(string, Node) error) error
}

var _ Directory = mapDirectory(nil)

type mapDirectory map[string]Node

func (md mapDirectory) Get(name string) (Node, bool) {
	node, ok := md[name]
	return node, ok
}

func (md mapDirectory) ForEach(fn func(string, Node) error) error {
	for name, node := range md {
		if err := fn(name, node); err != nil {
			return err
		}
	}
	return nil
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

func (*pprofDirectory) ForEach(fn func(string, Node) error) error {
	for _, p := range pprof.Profiles() {
		if err := fn(p.Name(), &FileWrapper{
			File: &pprofFile{
				p: p,
			},
		}); err != nil {
			return err
		}
	}
	return nil
}

type DirectoryWrapper struct {
	Directory Directory
}

var _ Node = (*DirectoryWrapper)(nil)

func (dir *DirectoryWrapper) GetDirectory() io.DirectoryWithCtx {
	return &directoryState{DirectoryWrapper: dir}
}

func (dir *DirectoryWrapper) getIO() (io.NodeWithCtx, func() error, error) {
	return dir.GetDirectory(), noop, nil
}

func (dir *DirectoryWrapper) addConnection(ctx fidl.Context, flags io.OpenFlags, mode uint32, req io.NodeWithCtxInterfaceRequest) error {
	ioDir := dir.GetDirectory()
	stub := io.DirectoryWithCtxStub{Impl: ioDir}
	go Serve(context.Background(), &stub, req.Channel, ServeOptions{
		OnError: logError,
	})
	return respond(ctx, flags, req, nil, dir)
}

var _ io.DirectoryWithCtx = (*directoryState)(nil)

type directoryState struct {
	*DirectoryWrapper

	reading bool
	dirents bytes.Buffer
}

func (dirState *directoryState) Clone(ctx fidl.Context, flags io.OpenFlags, req io.NodeWithCtxInterfaceRequest) error {
	return dirState.addConnection(ctx, flags, 0, req)
}

func (dirState *directoryState) Reopen(ctx fidl.Context, rights *io.RightsRequest, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (*directoryState) Close(fidl.Context) (unknown.CloseableCloseResult, error) {
	return unknown.CloseableCloseResultWithResponse(unknown.CloseableCloseResponse{}), nil
}

func (*DirectoryWrapper) DescribeDeprecated(fidl.Context) (io.NodeInfoDeprecated, error) {
	var nodeInfo io.NodeInfoDeprecated
	nodeInfo.SetDirectory(io.DirectoryObject{})
	return nodeInfo, nil
}

func (*directoryState) GetConnectionInfo(fidl.Context) (io.ConnectionInfo, error) {
	var connectionInfo io.ConnectionInfo
	// TODO(https://fxbug.dev/77623): Populate the rights requested by the client at connection.
	rights := io.RStarDir
	connectionInfo.SetRights(rights)
	return connectionInfo, nil
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

func (*directoryState) SetAttr(fidl.Context, io.NodeAttributeFlags, io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*directoryState) GetAttributes(fidl.Context, io.NodeAttributesQuery) (io.Node2GetAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2GetAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*directoryState) UpdateAttributes(fidl.Context, io.MutableNodeAttributes) (io.Node2UpdateAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2UpdateAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

const dot = "."

func (dirState *directoryState) Open(ctx fidl.Context, flags io.OpenFlags, mode uint32, path string, req io.NodeWithCtxInterfaceRequest) error {
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
			proxy, cleanup, err := node.getIO()
			if err != nil {
				return err
			}
			defer cleanup()
			if dir, ok := proxy.(io.DirectoryWithCtx); ok {
				return dir.Open(ctx, flags, mode, path[i+len(slash):], req)
			}
			return respond(ctx, flags, req, &zx.Error{Status: zx.ErrNotDir}, node)
		}
	} else if node, ok := dirState.Directory.Get(path); ok {
		return node.addConnection(ctx, flags, mode, req)
	}

	return respond(ctx, flags, req, &zx.Error{Status: zx.ErrNotFound}, dirState)
}

func (dirState *directoryState) Open2(ctx fidl.Context, path string, protocols io.ConnectionProtocols, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (*directoryState) AddInotifyFilter(ctx fidl.Context, path string, filters io.InotifyWatchMask, wd uint32, socket zx.Socket) error {
	return nil
}

func (*directoryState) Unlink(fidl.Context, string, io.UnlinkOptions) (io.Directory2UnlinkResult, error) {
	return io.Directory2UnlinkResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (dirState *directoryState) Enumerate(ctx fidl.Context, options io.DirectoryEnumerateOptions, req io.DirectoryIteratorWithCtxInterfaceRequest) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = req.Close()
	return nil
}

func (dirState *directoryState) ReadDirents(ctx fidl.Context, maxOut uint64) (int32, []uint8, error) {
	if !dirState.reading {
		writeFn := func(name string, node Node) error {
			ioNode, cleanup, err := node.getIO()
			if err != nil {
				return err
			}
			defer cleanup()
			status, attr, err := ioNode.GetAttr(ctx)
			if err != nil {
				return err
			}
			if status := zx.Status(status); status != zx.ErrOk {
				return fmt.Errorf("io.Node.GetAttr returned non-ok zx.Status %s", status)
			}
			dirent := syscall.Dirent{
				Ino:  attr.Id,
				Size: uint8(len(name)),
				Type: uint8(func() io.DirentType {
					switch modeType := attr.Mode & io.ModeTypeMask; modeType {
					case io.ModeTypeDirectory:
						return io.DirentTypeDirectory
					case io.ModeTypeFile:
						return io.DirentTypeFile
					case io.ModeTypeService:
						return io.DirentTypeService
					default:
						panic(fmt.Sprintf("unknown mode type: %b", modeType))
					}
				}()),
			}
			if err := binary.Write(&dirState.dirents, binary.LittleEndian, dirent); err != nil {
				return err
			}
			dirState.dirents.Truncate(dirState.dirents.Len() - int(unsafe.Sizeof(syscall.Dirent{}.Name)))
			if _, err := dirState.dirents.WriteString(name); err != nil {
				return err
			}
			return nil
		}
		if err := writeFn(dot, dirState); err != nil {
			return 0, nil, err
		}
		if err := dirState.Directory.ForEach(writeFn); err != nil {
			return 0, nil, err
		}
		dirState.reading = true
	} else if dirState.dirents.Len() == 0 {
		status, err := dirState.Rewind(ctx)
		if err != nil {
			return 0, nil, err
		}
		if status := zx.Status(status); status != zx.ErrOk {
			return 0, nil, fmt.Errorf("dirState.Rewind(_) = %s", status)
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

func (*directoryState) Rename(fidl.Context, string, zx.Event, string) (io.Directory2RenameResult, error) {
	return io.Directory2RenameResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*directoryState) Link(fidl.Context, string, zx.Handle, string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*directoryState) Watch(_ fidl.Context, _ io.WatchMask, _ uint32, watcher io.DirectoryWatcherWithCtxInterfaceRequest) (int32, error) {
	if err := watcher.Close(); err != nil {
		logError(err)
	}
	return int32(zx.ErrNotSupported), nil
}

func (*directoryState) GetFlags(fidl.Context) (int32, io.OpenFlags, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*directoryState) SetFlags(fidl.Context, io.OpenFlags) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (dirState *directoryState) AdvisoryLock(fidl.Context, io.AdvisoryLockRequest) (io.AdvisoryLockingAdvisoryLockResult, error) {
	return io.AdvisoryLockingAdvisoryLockResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*directoryState) QueryFilesystem(fidl.Context) (int32, *io.FilesystemInfo, error) {
	return int32(zx.ErrNotSupported), nil, nil
}

func (*directoryState) Query(fidl.Context) ([]uint8, error) {
	return []byte(io.DirectoryProtocolName), nil
}

type File interface {
	// If error is non-nil, GetReader returns a component.Reader for the file
	// contents, and the length of the file contents.
	GetReader() (Reader, uint64, error)
}

var _ File = (*pprofFile)(nil)

type pprofFile struct {
	p *pprof.Profile
}

func (p *pprofFile) GetReader() (Reader, uint64, error) {
	var b bytes.Buffer
	if err := p.p.WriteTo(&b, 0); err != nil {
		return nil, 0, err
	}
	return NoVMO(NopCloser(bytes.NewReader(b.Bytes()))), uint64(b.Len()), nil
}

var _ File = (*stackTraceFile)(nil)

// stackTraceFile provides a File implementation to expose goroutine
// stacks.
type stackTraceFile struct{}

func (f *stackTraceFile) GetReader() (Reader, uint64, error) {
	buf := make([]byte, 4096)
	for {
		n := runtime.Stack(buf, true)
		if n < len(buf) {
			return NoVMO(NopCloser(bytes.NewReader(buf[:n]))), uint64(n), nil
		}
		buf = make([]byte, 2*len(buf))
	}
}

var _ Node = (*FileWrapper)(nil)

type FileWrapper struct {
	File File
}

func (file *FileWrapper) getFileState() (*fileState, error) {
	reader, size, err := file.File.GetReader()
	if err != nil {
		return nil, err
	}
	return &fileState{
		FileWrapper: file,
		reader:      reader,
		size:        size,
	}, nil
}

func (file *FileWrapper) GetFile() (io.FileWithCtx, error) {
	return file.getFileState()
}

func (file *FileWrapper) getIO() (io.NodeWithCtx, func() error, error) {
	state, err := file.getFileState()
	if err != nil {
		return nil, noop, err
	}
	return state, state.reader.Close, nil
}

func (file *FileWrapper) addConnection(ctx fidl.Context, flags io.OpenFlags, mode uint32, req io.NodeWithCtxInterfaceRequest) error {
	ioFile, err := file.getFileState()
	if err != nil {
		return err
	}
	stub := io.FileWithCtxStub{Impl: ioFile}
	go func() {
		defer ioFile.reader.Close()
		Serve(context.Background(), &stub, req.Channel, ServeOptions{
			OnError: logError,
		})
	}()
	return respond(ctx, flags, req, nil, ioFile)
}

var _ io.FileWithCtx = (*fileState)(nil)

type ReaderWithoutCloser interface {
	stdio.Reader
	stdio.ReaderAt
	stdio.Seeker
}

type ReaderWithoutGetVMO interface {
	ReaderWithoutCloser
	stdio.Closer
}

type Reader interface {
	ReaderWithoutGetVMO
	// GetVMO returns a pointer to the Reader's handle to its backing VMO, if the
	// Reader is backed by a VMO.
	GetVMO() *zx.VMO
}

type nopCloser struct {
	ReaderWithoutCloser
}

func (nopCloser) Close() error { return nil }

// NopCloser implements Closer for Readers that don't need closing. We can't just use io.NopCloser()
// because it doesn't compose with stdio.ReaderAt.
func NopCloser(r ReaderWithoutCloser) ReaderWithoutGetVMO {
	return nopCloser{ReaderWithoutCloser: r}
}

type noVMO struct {
	ReaderWithoutGetVMO
}

func (*noVMO) GetVMO() *zx.VMO {
	return nil
}

// NoVMO implements GetVMO for Readers that don't have a backing VMO.
func NoVMO(r ReaderWithoutGetVMO) Reader {
	return &noVMO{ReaderWithoutGetVMO: r}
}

type fileState struct {
	*FileWrapper
	reader Reader
	size   uint64
}

func (fState *fileState) Clone(ctx fidl.Context, flags io.OpenFlags, req io.NodeWithCtxInterfaceRequest) error {
	return fState.addConnection(ctx, flags, 0, req)
}

func (fState *fileState) Reopen(ctx fidl.Context, rights *io.RightsRequest, channel zx.Channel) error {
	// TODO(https://fxbug.dev/77623): implement.
	_ = channel.Close()
	return nil
}

func (fState *fileState) Close(fidl.Context) (unknown.CloseableCloseResult, error) {
	return unknown.CloseableCloseResultWithResponse(unknown.CloseableCloseResponse{}), fState.reader.Close()
}

func (*FileWrapper) DescribeDeprecated(fidl.Context) (io.NodeInfoDeprecated, error) {
	var nodeInfo io.NodeInfoDeprecated
	nodeInfo.SetFile(io.FileObject{})
	return nodeInfo, nil
}

func (fState *fileState) Describe2(fidl.Context) (io.FileInfo, error) {
	var fileInfo io.FileInfo
	return fileInfo, nil
}

func (fState *fileState) GetConnectionInfo(fidl.Context) (io.ConnectionInfo, error) {
	var connectionInfo io.ConnectionInfo
	// TODO(https://fxbug.dev/77623): Populate the rights requested by the client at connection.
	rights := io.RStarDir
	connectionInfo.SetRights(rights)
	return connectionInfo, nil
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

func (*fileState) SetAttr(fidl.Context, io.NodeAttributeFlags, io.NodeAttributes) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*fileState) GetAttributes(fidl.Context, io.NodeAttributesQuery) (io.Node2GetAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2GetAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*fileState) UpdateAttributes(fidl.Context, io.MutableNodeAttributes) (io.Node2UpdateAttributesResult, error) {
	// TODO(https://fxbug.dev/77623): implement.
	return io.Node2UpdateAttributesResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (fState *fileState) Read(_ fidl.Context, count uint64) (io.ReadableReadResult, error) {
	if l := fState.size; l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.reader.Read(b)
	if err != nil && err != stdio.EOF {
		return io.ReadableReadResult{}, err
	}
	b = b[:n]
	return io.ReadableReadResultWithResponse(io.ReadableReadResponse{
		Data: b,
	}), nil
}

func (fState *fileState) ReadAt(_ fidl.Context, count uint64, offset uint64) (io.FileReadAtResult, error) {
	if l := fState.size - offset; l < count {
		count = l
	}
	b := make([]byte, count)
	n, err := fState.reader.ReadAt(b, int64(offset))
	if err != nil && err != stdio.EOF {
		return io.FileReadAtResult{}, err
	}
	b = b[:n]
	return io.FileReadAtResultWithResponse(io.FileReadAtResponse{
		Data: b,
	}), nil
}

func (*fileState) Write(fidl.Context, []uint8) (io.WritableWriteResult, error) {
	return io.WritableWriteResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*fileState) WriteAt(fidl.Context, []uint8, uint64) (io.FileWriteAtResult, error) {
	return io.FileWriteAtResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (fState *fileState) Seek(_ fidl.Context, origin io.SeekOrigin, offset int64) (io.FileSeekResult, error) {
	n, err := fState.reader.Seek(offset, int(origin))
	return io.FileSeekResultWithResponse(
		io.FileSeekResponse{
			OffsetFromStart: uint64(n),
		}), err
}

func (*fileState) Resize(fidl.Context, uint64) (io.FileResizeResult, error) {
	return io.FileResizeResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (*fileState) GetFlags(fidl.Context) (int32, io.OpenFlags, error) {
	return int32(zx.ErrNotSupported), 0, nil
}

func (*fileState) SetFlags(fidl.Context, io.OpenFlags) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (*fileState) QueryFilesystem(fidl.Context) (int32, *io.FilesystemInfo, error) {
	return int32(zx.ErrNotSupported), nil, nil
}

func (*fileState) Query(fidl.Context) ([]byte, error) {
	return []byte(io.FileProtocolName), nil
}

func (fState *fileState) AdvisoryLock(fidl.Context, io.AdvisoryLockRequest) (io.AdvisoryLockingAdvisoryLockResult, error) {
	return io.AdvisoryLockingAdvisoryLockResult{}, &zx.Error{Status: zx.ErrNotSupported, Text: fmt.Sprintf("%T", fState)}
}

func (fState *fileState) GetBackingMemory(fidl.Context, io.VmoFlags) (io.FileGetBackingMemoryResult, error) {
	if vmo := fState.reader.GetVMO(); vmo != nil {
		// TODO(https://fxbug.dev/77623): The rights on this VMO should be capped at the connection's.
		h, err := vmo.Handle().Duplicate(zx.RightSameRights)
		switch err := err.(type) {
		case nil:
			return io.FileGetBackingMemoryResultWithResponse(io.FileGetBackingMemoryResponse{
				Vmo: zx.VMO(h),
			}), nil
		case *zx.Error:
			return io.FileGetBackingMemoryResultWithErr(int32(err.Status)), nil
		default:
			return io.FileGetBackingMemoryResult{}, err
		}
	}
	return io.FileGetBackingMemoryResultWithErr(int32(zx.ErrNotSupported)), nil
}
