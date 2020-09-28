// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package rpc

import (
	"context"
	"fmt"
	"log"
	"os"
	"sync"
	"syscall"
	"syscall/zx"
	"syscall/zx/fidl"
	"time"
	"unsafe"

	"fidl/fuchsia/io"
	"fidl/fuchsia/mem"

	"go.fuchsia.dev/fuchsia/garnet/go/src/thinfs/fs"
	"go.fuchsia.dev/fuchsia/src/lib/component"
)

const (
	statusFlags = uint32(syscall.FsFlagAppend)
	rightFlags  = uint32(syscall.FsRightReadable | syscall.FsRightWritable | syscall.FsFlagPath)
)

type key uint64

type ThinVFS struct {
	sync.Mutex
	dirs      map[key]*directoryWrapper
	nextToken key
	fs        fs.FileSystem
}

type VFSQueryInfo struct {
	TotalBytes uint64
	UsedBytes  uint64
	TotalNodes uint64
	UsedNodes  uint64
}

// NewServer creates a new ThinVFS server. Serve must be called to begin servicing the filesystem.
func NewServer(filesys fs.FileSystem, c zx.Channel) (*ThinVFS, error) {
	vfs := &ThinVFS{
		dirs: make(map[key]*directoryWrapper),
		fs:   filesys,
	}
	ireq := io.NodeWithCtxInterfaceRequest{Channel: c}
	vfs.addDirectory(filesys.RootDirectory(), ireq)
	// Signal that we're ready to serve.
	h := zx.Handle(c)
	if err := h.SignalPeer(0, zx.SignalUser0); err != nil {
		_ = h.Close()
		return nil, err
	}
	return vfs, nil
}

func (vfs *ThinVFS) addDirectory(dir fs.Directory, node io.NodeWithCtxInterfaceRequest) {
	d := directoryWrapper{vfs: vfs, dir: dir, cookies: make(map[uint64]uint64)}
	vfs.Lock()
	tok := vfs.nextToken
	vfs.nextToken++
	vfs.dirs[tok] = &d
	vfs.Unlock()
	d.token = tok
	go func() {
		defer func() {
			vfs.Lock()
			delete(vfs.dirs, d.token)
			vfs.Unlock()
		}()
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()

		d.cancel = cancel
		stub := io.DirectoryWithCtxStub{Impl: &d}
		component.ServeExclusive(ctx, &stub, node.Channel, func(err error) { log.Print(err) })
	}()
}

func (vfs *ThinVFS) addFile(file fs.File, node io.NodeWithCtxInterfaceRequest) {
	go func() {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		f := fileWrapper{
			vfs:    vfs,
			cancel: cancel,
			file:   file,
		}
		stub := io.FileWithCtxStub{Impl: &f}
		component.ServeExclusive(ctx, &stub, node.Channel, func(err error) { log.Print(err) })
	}()
}

type directoryWrapper struct {
	vfs     *ThinVFS
	token   key
	cancel  context.CancelFunc
	dir     fs.Directory
	dirents []fs.Dirent
	reading bool
	e       zx.Event
	cookies map[uint64]uint64
}

func getKoid(h zx.Handle) uint64 {
	info, err := h.GetInfoHandleBasic()
	if err != nil {
		return 0
	}
	return info.Koid
}

func (d *directoryWrapper) getCookie(h zx.Handle) uint64 {
	return d.cookies[getKoid(h)]
}

func (d *directoryWrapper) setCookie(cookie uint64) {
	d.cookies[getKoid(zx.Handle(d.e))] = cookie
}

func (d *directoryWrapper) clearCookie() {
	if zx.Handle(d.e) != zx.HandleInvalid {
		delete(d.cookies, getKoid(zx.Handle(d.e)))
	}
}

func (d *directoryWrapper) Clone(_ fidl.Context, flags uint32, node io.NodeWithCtxInterfaceRequest) error {
	newDir, err := d.dir.Dup()
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		d.vfs.addDirectory(newDir, node)
	}
	// Only send an OnOpen message if OpenFlagDescribe is set.
	if flags&io.OpenFlagDescribe != 0 {
		c := fidl.InterfaceRequest(node).Channel
		pxy := io.NodeEventProxy(fidl.ChannelProxy{Channel: c})
		var info io.NodeInfo
		info.SetDirectory(io.DirectoryObject{})
		return pxy.OnOpen(int32(zxErr), &info)
	}
	return nil
}

func (d *directoryWrapper) Close(fidl.Context) (int32, error) {
	err := d.dir.Close()

	d.cancel()
	d.clearCookie()

	return int32(errorToZx(err)), nil
}

func (d *directoryWrapper) ListInterfaces(fidl.Context) ([]string, error) {
	return nil, nil
}

func (d *directoryWrapper) Describe(fidl.Context) (io.NodeInfo, error) {
	var info io.NodeInfo
	info.SetDirectory(io.DirectoryObject{})
	return info, nil
}

func (d *directoryWrapper) Sync(fidl.Context) (int32, error) {
	return int32(errorToZx(d.dir.Sync())), nil
}

func (d *directoryWrapper) GetAttr(fidl.Context) (int32, io.NodeAttributes, error) {
	size, _, mtime, err := d.dir.Stat()
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return int32(zxErr), io.NodeAttributes{}, nil
	}
	return int32(zx.ErrOk), io.NodeAttributes{
		Mode:             syscall.S_IFDIR | 0755,
		Id:               1,
		ContentSize:      uint64(size),
		StorageSize:      uint64(size),
		LinkCount:        1,
		CreationTime:     uint64(mtime.Unix()),
		ModificationTime: uint64(mtime.Unix()),
	}, nil
}

func (d *directoryWrapper) SetAttr(_ fidl.Context, flags uint32, attr io.NodeAttributes) (int32, error) {
	t := time.Unix(0, int64(attr.ModificationTime))
	return int32(errorToZx(d.dir.Touch(t, t))), nil
}

func (d *directoryWrapper) Open(_ fidl.Context, inFlags, inMode uint32, path string, node io.NodeWithCtxInterfaceRequest) error {
	flags := openFlagsFromFIDL(inFlags, inMode)
	if flags.Path() {
		flags &= fs.OpenFlagPath | fs.OpenFlagDirectory | fs.OpenFlagDescribe
	}
	fsFile, fsDir, fsRemote, err := d.dir.Open(path, flags)

	// Handle the case of a remote, and just forward.
	if fsRemote != nil {
		fwd := ((*io.DirectoryWithCtxInterface)(&fidl.ChannelProxy{Channel: fsRemote.Channel}))
		flags, mode := openFlagsToFIDL(fsRemote.Flags)
		if inFlags&io.OpenFlagDescribe != 0 {
			flags |= io.OpenFlagDescribe
		}
		return fwd.Open(context.Background(), flags, mode, fsRemote.Path, node)
	}

	// Handle the file and directory cases. They're mostly the same, except where noted.
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		if fsFile != nil {
			d.vfs.addFile(fsFile, node)
		} else {
			d.vfs.addDirectory(fsDir, node)
		}
	} else {
		// We got an error, so we want to make sure we close the channel.
		// However, if we were asked to Describe, we might still want to notify
		// about the status, so just close on exit.
		c := fidl.InterfaceRequest(node).Channel
		defer c.Close()
	}

	// Only send an OnOpen message if OpenFlagDescribe is set.
	if inFlags&io.OpenFlagDescribe != 0 {
		var info io.NodeInfo
		if fsFile != nil {
			info.SetFile(io.FileObject{
				Event: zx.Event(zx.HandleInvalid),
			})
		} else {
			info.SetDirectory(io.DirectoryObject{})
		}
		c := fidl.InterfaceRequest(node).Channel
		pxy := io.NodeEventProxy(fidl.ChannelProxy{Channel: c})
		return pxy.OnOpen(int32(zxErr), &info)
	}

	return nil
}

func (d *directoryWrapper) Unlink(_ fidl.Context, path string) (int32, error) {
	return int32(errorToZx(d.dir.Unlink(path))), nil
}

const direntSize = int(unsafe.Offsetof(syscall.Dirent{}.Name))

func (d *directoryWrapper) ReadDirents(_ fidl.Context, maxOut uint64) (int32, []byte, error) {
	if maxOut > io.MaxBuf {
		return int32(zx.ErrInvalidArgs), nil, nil
	}
	if d.reading && len(d.dirents) == 0 {
		d.reading = false
		return int32(zx.ErrOk), nil, nil
	}
	if !d.reading {
		dirents, err := d.dir.Read()
		if zxErr := errorToZx(err); zxErr != zx.ErrOk {
			return int32(zxErr), nil, nil
		}
		d.reading = true
		d.dirents = dirents
	}
	bytes := make([]byte, maxOut)
	var written int
	var i int
	for i = range d.dirents {
		dirent := d.dirents[i]
		sysDirent := syscall.Dirent{}
		name := dirent.GetName()
		size := direntSize + len(name)
		sysDirent.Ino = dirent.GetIno()
		sysDirent.Size = uint8(len(name))
		sysDirent.Type = uint8((fileTypeToFIDL(dirent.GetType()) >> 12) & 15)
		if uint64(written+size) > maxOut {
			break
		}
		copy(bytes[written:], (*(*[direntSize]byte)(unsafe.Pointer(&sysDirent)))[:])
		copy(bytes[written+direntSize:], name)
		written += size
	}
	if i == len(d.dirents)-1 { // We finished reading the directory. Next readdir will be empty.
		d.dirents = nil
	} else { // Partial read
		d.dirents = d.dirents[i:]
	}
	d.reading = true
	return int32(zx.ErrOk), bytes[:written], nil
}

func (d *directoryWrapper) Rewind(fidl.Context) (int32, error) {
	d.reading = false
	return int32(zx.ErrOk), nil
}

func (d *directoryWrapper) GetToken(fidl.Context) (int32, zx.Handle, error) {
	d.vfs.Lock()
	defer d.vfs.Unlock()
	if d.e != 0 {
		if e, err := d.e.Duplicate(zx.RightSameRights); err != nil {
			return int32(errorToZx(err)), zx.HandleInvalid, nil
		} else {
			return int32(zx.ErrOk), zx.Handle(e), nil
		}
	}

	// Create a new event which may later be used to refer to this node
	e0, err := zx.NewEvent(0)
	if err != nil {
		return int32(errorToZx(err)), zx.HandleInvalid, nil
	}

	d.e = e0

	// One handle to the event returns to the client, one end is kept on the
	// server (and is accessible within the cookie).
	var e1 zx.Event
	if e1, err = e0.Duplicate(zx.RightSameRights); err != nil {
		goto fail_event_created
	}
	d.setCookie(uint64(d.token))
	return int32(zx.ErrOk), zx.Handle(e1), nil

	e1.Close()
fail_event_created:
	e0.Close()
	d.e = zx.Event(zx.HandleInvalid)
	return int32(errorToZx(err)), zx.HandleInvalid, nil
}

func (d *directoryWrapper) Rename(_ fidl.Context, src string, token zx.Handle, dst string) (int32, error) {
	if len(src) < 1 || len(dst) < 1 {
		return int32(zx.ErrInvalidArgs), nil
	}
	d.vfs.Lock()
	defer d.vfs.Unlock()
	cookie := d.getCookie(token)
	if cookie == 0 {
		return int32(zx.ErrInvalidArgs), nil
	}
	dir, ok := d.vfs.dirs[key(cookie)]
	if !ok {
		return int32(zx.ErrInvalidArgs), nil
	}
	return int32(errorToZx(d.dir.Rename(dir.dir, src, dst))), nil
}

func (d *directoryWrapper) Link(_ fidl.Context, src string, token zx.Handle, dst string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) Watch(_ fidl.Context, mask uint32, options uint32, watcher zx.Channel) (int32, error) {
	watcher.Close()
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) Mount(_ fidl.Context, remote io.DirectoryWithCtxInterfaceRequest) (int32, error) {
	remote.Close()
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) MountAndCreate(_ fidl.Context, remote io.DirectoryWithCtxInterfaceRequest, name string, flags uint32) (int32, error) {
	remote.Close()
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) Unmount(fidl.Context) (int32, error) {
	// Shut down filesystem
	err := d.vfs.fs.Close()
	if err != nil {
		fmt.Printf("error unmounting filesystem: %#v\n", err)
	}
	os.Exit(0)
	return int32(zx.ErrOk), nil
}

func (d *directoryWrapper) UnmountNode(fidl.Context) (int32, io.DirectoryWithCtxInterfaceRequest, error) {
	return int32(zx.ErrNotSupported), io.DirectoryWithCtxInterfaceRequest(fidl.InterfaceRequest{Channel: zx.Channel(zx.HandleInvalid)}), nil
}

func (d *directoryWrapper) QueryFilesystem(fidl.Context) (int32, *io.FilesystemInfo, error) {
	totalBytes := uint64(d.vfs.fs.Size())
	usedBytes := totalBytes - uint64(d.vfs.fs.FreeSize())

	info := io.FilesystemInfo{
		TotalBytes:      totalBytes,
		UsedBytes:       usedBytes,
		TotalNodes:      0,
		UsedNodes:       0,
		FsId:            0,
		BlockSize:       0,
		MaxFilenameSize: 255,
		FsType:          0,
		Padding:         0,
	}
	name := d.vfs.fs.Type()
	nameData := *(*[]int8)(unsafe.Pointer(&name))
	copy(info.Name[:], nameData)
	info.Name[len(name)] = 0
	return int32(zx.ErrOk), &info, nil
}

// NodeGetFlags is a transitional method, and if it's unimplemented the current behavior
// of the FIDL bindings is to crash the server, which is undesirable.
// Given that it's a transitional method, we can choose not to implement it fully.
// TODO(fxbug.dev/55663): remove this method when the default behavior of the FIDL bindings is changed
func (d *directoryWrapper) NodeGetFlags(fidl.Context) (int32, uint32, error) {
	return int32(zx.ErrNotSupported), uint32(0), nil
}

// NodeSetFlags is a transitional method, and if it's unimplemented the current behavior
// of the FIDL bindings is to crash the server, which is undesirable.
// Given that it's a transitional method, we can choose not to implement it fully.
// TODO(fxbug.dev/55663): remove this method when the default behavior of the FIDL bindings is changed
func (d *directoryWrapper) NodeSetFlags(fidl.Context, uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

type fileWrapper struct {
	vfs    *ThinVFS
	cancel context.CancelFunc
	file   fs.File
}

func (f *fileWrapper) Clone(_ fidl.Context, flags uint32, node io.NodeWithCtxInterfaceRequest) error {
	newFile, err := f.file.Dup()
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		f.vfs.addFile(newFile, node)
	}
	// Only send an OnOpen message if OpenFlagDescribe is set.
	if flags&io.OpenFlagDescribe != 0 {
		c := fidl.InterfaceRequest(node).Channel
		pxy := io.NodeEventProxy(fidl.ChannelProxy{Channel: c})
		var info io.NodeInfo
		info.SetFile(io.FileObject{
			Event: zx.Event(zx.HandleInvalid),
		})
		return pxy.OnOpen(int32(zx.ErrOk), &info)
	}
	return nil
}

func (f *fileWrapper) Close(fidl.Context) (int32, error) {
	err := f.file.Close()

	f.cancel()

	return int32(errorToZx(err)), nil
}

func (f *fileWrapper) ListInterfaces(fidl.Context) ([]string, error) {
	return nil, nil
}

func (f *fileWrapper) Describe(fidl.Context) (io.NodeInfo, error) {
	var info io.NodeInfo
	info.SetFile(io.FileObject{
		Event: zx.Event(zx.HandleInvalid),
	})
	return info, nil
}

func (f *fileWrapper) Sync(fidl.Context) (int32, error) {
	return int32(errorToZx(f.file.Sync())), nil
}

func (f *fileWrapper) GetAttr(fidl.Context) (int32, io.NodeAttributes, error) {
	size, _, mtime, err := f.file.Stat()
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return int32(zxErr), io.NodeAttributes{}, nil
	}
	return int32(zx.ErrOk), io.NodeAttributes{
		Mode:             syscall.S_IFREG | 0644,
		Id:               1,
		ContentSize:      uint64(size),
		StorageSize:      uint64(size),
		LinkCount:        1,
		CreationTime:     uint64(mtime.Unix()),
		ModificationTime: uint64(mtime.Unix()),
	}, nil
}

func (f *fileWrapper) SetAttr(_ fidl.Context, flags uint32, attr io.NodeAttributes) (int32, error) {
	if f.file.GetOpenFlags().Path() {
		return int32(zx.ErrBadHandle), nil
	}
	t := time.Unix(0, int64(attr.ModificationTime))
	return int32(errorToZx(f.file.Touch(t, t))), nil
}

func (f *fileWrapper) Read(_ fidl.Context, count uint64) (int32, []uint8, error) {
	buf := make([]byte, count)
	r, err := f.file.Read(buf, 0, fs.WhenceFromCurrent)
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return int32(zxErr), nil, nil
	}
	return int32(zx.ErrOk), buf[:r], nil
}

func (f *fileWrapper) ReadAt(_ fidl.Context, count, offset uint64) (int32, []uint8, error) {
	buf := make([]byte, count)
	r, err := f.file.Read(buf, int64(offset), fs.WhenceFromStart)
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return int32(zxErr), nil, nil
	}
	return int32(zx.ErrOk), buf[:r], nil
}

func (f *fileWrapper) Write(_ fidl.Context, data []uint8) (int32, uint64, error) {
	r, err := f.file.Write(data, 0, fs.WhenceFromCurrent)
	return int32(errorToZx(err)), uint64(r), nil
}

func (f *fileWrapper) WriteAt(_ fidl.Context, data []uint8, offset uint64) (int32, uint64, error) {
	r, err := f.file.Write(data, int64(offset), fs.WhenceFromStart)
	return int32(errorToZx(err)), uint64(r), nil
}

func (f *fileWrapper) Seek(_ fidl.Context, offset int64, start io.SeekOrigin) (int32, uint64, error) {
	r, err := f.file.Seek(offset, int(start))
	return int32(errorToZx(err)), uint64(r), nil
}

func (f *fileWrapper) Truncate(_ fidl.Context, length uint64) (int32, error) {
	return int32(errorToZx(f.file.Truncate(length))), nil
}

func (f *fileWrapper) getFlagsInternal(fidl.Context) (int32, uint32, error) {
	oflags := uint32(f.file.GetOpenFlags())
	return int32(zx.ErrOk), oflags & (rightFlags | statusFlags), nil
}

func (f *fileWrapper) GetFlags(ctx fidl.Context) (int32, uint32, error) {
	return f.getFlagsInternal(ctx)
}

func (f *fileWrapper) NodeGetFlags(ctx fidl.Context) (int32, uint32, error) {
	return f.getFlagsInternal(ctx)
}

// NodeSetFlags is a transitional method, and if it's unimplemented the current behavior
// of the FIDL bindings is to crash the server, which is undesirable.
// Given that it's a transitional method, we can choose not to implement it fully.
// TODO(fxbug.dev/55663): remove this method when the default behavior of the FIDL bindings is changed
func (f *fileWrapper) NodeSetFlags(ctx fidl.Context, flags uint32) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (f *fileWrapper) SetFlags(_ fidl.Context, inFlags uint32) (int32, error) {
	flags := uint32(openFlagsFromFIDL(inFlags, 0))
	uflags := (uint32(f.file.GetOpenFlags()) & ^statusFlags) | (flags & statusFlags)
	return int32(errorToZx(f.file.SetOpenFlags(fs.OpenFlags(uflags)))), nil
}

func (f *fileWrapper) GetBuffer(_ fidl.Context, flags uint32) (int32, *mem.Buffer, error) {
	if file, ok := f.file.(fs.FileWithGetBuffer); ok {
		buf, err := file.GetBuffer(flags)
		return int32(errorToZx(err)), buf, err
	}
	return int32(zx.ErrNotSupported), nil, nil
}

// TODO(smklein): Calibrate thinfs flags with standard C library flags to make conversion smoother

func errorToZx(err error) zx.Status {
	switch e := err.(type) {
	case *zx.Error:
		return e.Status
	}

	switch err {
	case nil, fs.ErrEOF:
		// ErrEOF can be translated directly to ErrOk. For operations which return with an error if
		// partially complete (such as 'Read'), RemoteIO does not flag an error -- instead, it
		// simply returns the number of bytes which were processed.
		return zx.ErrOk
	case fs.ErrInvalidArgs:
		return zx.ErrInvalidArgs
	case fs.ErrNotFound:
		return zx.ErrNotFound
	case fs.ErrAlreadyExists:
		return zx.ErrAlreadyExists
	case fs.ErrPermission, fs.ErrReadOnly:
		// We're returning "BadHandle" instead of "AccessDenied"
		// to match the POSIX convention where bad fd permissions
		// typically return "EBADF".
		return zx.ErrBadHandle
	case fs.ErrNoSpace:
		return zx.ErrNoSpace
	case fs.ErrNotEmpty:
		return zx.ErrNotEmpty
	case fs.ErrFailedPrecondition, fs.ErrNotEmpty, fs.ErrNotOpen, fs.ErrIsActive, fs.ErrUnmounted:
		return zx.ErrBadState
	case fs.ErrNotAFile:
		return zx.ErrNotFile
	case fs.ErrNotADir:
		return zx.ErrNotDir
	case fs.ErrNotSupported:
		return zx.ErrNotSupported
	default:
		return zx.ErrInternal
	}
}

func fileTypeToFIDL(t fs.FileType) uint32 {
	switch t {
	case fs.FileTypeRegularFile:
		return syscall.S_IFREG
	case fs.FileTypeDirectory:
		return syscall.S_IFDIR
	default:
		return 0
	}
}

const alignedFlags = uint32(syscall.FdioAlignedFlags | syscall.FsRightReadable | syscall.FsRightWritable | syscall.FsRightExecutable)

func openFlagsToFIDL(f fs.OpenFlags) (arg uint32, mode uint32) {
	arg = uint32(f) & alignedFlags

	if (f.Create() && !f.Directory()) || f.File() {
		mode |= syscall.S_IFREG
	}
	if f.Directory() {
		mode |= syscall.S_IFDIR
	}
	return
}

func openFlagsFromFIDL(arg uint32, mode uint32) fs.OpenFlags {
	res := fs.OpenFlags(arg & alignedFlags)

	// Additional open flags
	if arg&syscall.FsFlagCreate != 0 {
		res |= fs.OpenFlagCreate
		res |= fs.OpenFlagWrite | fs.OpenFlagRead
	}

	// Ad-hoc mechanism for additional file access mode flags
	switch mode & syscall.S_IFMT {
	case syscall.S_IFDIR:
		res |= fs.OpenFlagDirectory
	case syscall.S_IFREG:
		res |= fs.OpenFlagFile
	}

	if res.Create() && !res.Directory() {
		res |= fs.OpenFlagFile
	}

	return res
}
