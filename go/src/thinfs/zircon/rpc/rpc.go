// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package rpc

import (
	"fmt"
	"os"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"thinfs/fs"

	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/io"
)

const (
	statusFlags = uint32(syscall.FsFlagAppend)
	rightFlags  = uint32(syscall.FsRightReadable | syscall.FsRightWritable | syscall.FsFlagPath)
)

type ThinVFS struct {
	sync.Mutex
	DirectoryService io.DirectoryService
	dirs             map[fidl.BindingKey]*directoryWrapper
	FileService      io.FileService
	fs               fs.FileSystem
}

type VFSQueryInfo struct {
	TotalBytes uint64
	UsedBytes  uint64
	TotalNodes uint64
	UsedNodes  uint64
}

// NewServer creates a new ThinVFS server. Serve must be called to begin servicing the filesystem.
func NewServer(filesys fs.FileSystem, h zx.Handle) (*ThinVFS, error) {
	vfs := &ThinVFS{
		dirs: make(map[fidl.BindingKey]*directoryWrapper),
		fs:   filesys,
	}
	ireq := io.ObjectInterfaceRequest(fidl.InterfaceRequest{Channel: zx.Channel(h)})
	if _, err := vfs.addDirectory(filesys.RootDirectory(), ireq); err != nil {
		h.Close()
		return nil, err
	}
	// Signal that we're ready to serve.
	if err := h.SignalPeer(0, zx.SignalUser0); err != nil {
		h.Close()
		return nil, err
	}
	return vfs, nil
}

// Serve begins dispatching fidl requests. Serve blocks, so callers will normally want to run it in a new goroutine.
func (vfs *ThinVFS) Serve() {
	fidl.Serve()
}

func (vfs *ThinVFS) addDirectory(dir fs.Directory, object io.ObjectInterfaceRequest) (fidl.BindingKey, error) {
	d := &directoryWrapper{vfs: vfs, dir: dir}

	vfs.Lock()
	defer vfs.Unlock()
	tok, err := d.vfs.DirectoryService.Add(
		d,
		(fidl.InterfaceRequest(object)).Channel,
		nil,
	)
	if err != nil {
		return 0, err
	}
	d.token = tok
	vfs.dirs[tok] = d
	return tok, nil
}

func (vfs *ThinVFS) addFile(file fs.File, object io.ObjectInterfaceRequest) (fidl.BindingKey, error) {
	f := &fileWrapper{vfs: vfs, file: file}

	vfs.Lock()
	defer vfs.Unlock()
	tok, err := vfs.FileService.Add(
		f,
		(fidl.InterfaceRequest(object)).Channel,
		nil,
	)
	if err != nil {
		return 0, err
	}
	f.token = tok
	return tok, nil
}

type directoryWrapper struct {
	vfs     *ThinVFS
	token   fidl.BindingKey
	dir     fs.Directory
	dirents []fs.Dirent
	reading bool
	e       zx.Event
}

func (d *directoryWrapper) Clone(flags uint32, object io.ObjectInterfaceRequest) error {
	newDir, err := d.dir.Dup()
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		_, err := d.vfs.addDirectory(newDir, object)
		if err != nil {
			return err
		}
	}
	// Only send an OnOpen message if OpenFlagDescribe is set.
	if flags&io.KOpenFlagDescribe != 0 {
		c := fidl.InterfaceRequest(object).Channel
		pxy := io.ObjectEventProxy(fidl.Proxy{Channel: c})
		info := &io.ObjectInfo{
			ObjectInfoTag: io.ObjectInfoDirectory,
		}
		return pxy.OnOpen(zxErr, info)
	}
	return nil
}

func (d *directoryWrapper) Close() (zx.Status, error) {
	err := d.dir.Close()

	d.vfs.Lock()
	defer d.vfs.Unlock()
	if zx.Handle(d.e) != zx.HandleInvalid {
		d.e.Handle().SetCookie(zx.ProcHandle, 0)
	}
	d.vfs.DirectoryService.Remove(d.token)
	delete(d.vfs.dirs, d.token)

	return errorToZx(err), nil
}

func (d *directoryWrapper) ListInterfaces() ([]string, error) {
	return nil, nil
}

func (d *directoryWrapper) Bind(iface string) error {
	return nil
}

func (d *directoryWrapper) Describe() (io.ObjectInfo, error) {
	return io.ObjectInfo{
		ObjectInfoTag: io.ObjectInfoDirectory,
	}, nil
}

func (d *directoryWrapper) Sync() (zx.Status, error) {
	return errorToZx(d.dir.Sync()), nil
}

func (d *directoryWrapper) GetAttr() (zx.Status, io.NodeAttributes, error) {
	size, _, mtime, err := d.dir.Stat()
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return zxErr, io.NodeAttributes{}, nil
	}
	return zx.ErrOk, io.NodeAttributes{
		Mode:             syscall.S_IFDIR | 0755,
		Id:               1,
		ContentSize:      uint64(size),
		StorageSize:      uint64(size),
		LinkCount:        1,
		CreationTime:     uint64(mtime.Unix()),
		ModificationTime: uint64(mtime.Unix()),
	}, nil
}

func (d *directoryWrapper) SetAttr(flags uint32, attr io.NodeAttributes) (zx.Status, error) {
	t := time.Unix(0, int64(attr.ModificationTime))
	return errorToZx(d.dir.Touch(t, t)), nil
}

func (d *directoryWrapper) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (zx.Status, []zx.Handle, []uint8, error) {
	switch opcode {
	case fdio.IoctlVFSGetTokenFS:
		// Legacy compatibility. Just call into GetToken directly.
		status, handle, err := d.GetToken()
		return status, []zx.Handle{handle}, nil, err
	case fdio.IoctlVFSUnmountFS:
		// Shut down filesystem
		err := d.vfs.fs.Close()
		if err != nil {
			fmt.Printf("error unmounting filesystem: %#v\n", err)
		}
		// While normally this would explode as the bindings will fail to
		// send a response, we exit immediately after, so it's OK.
		d.vfs.FileService.Close()
		d.vfs.DirectoryService.Close()
		os.Exit(0)
	case fdio.IoctlVFSQueryFS:
		totalBytes := uint64(d.vfs.fs.Size())
		usedBytes := totalBytes - uint64(d.vfs.fs.FreeSize())

		queryInfo := VFSQueryInfo{
			TotalBytes: totalBytes,
			UsedBytes:  usedBytes,
			TotalNodes: 0,
			UsedNodes:  0,
		}

		name := append([]byte(d.vfs.fs.Type()), 0)

		const infoSize = uint32(unsafe.Sizeof(queryInfo))
		totalSize := infoSize + uint32(len(name))
		if uint64(totalSize) > maxOut {
			return zx.ErrBufferTooSmall, nil, nil, nil
		}

		buf := make([]byte, totalSize)
		copy(buf[0:], (*[infoSize]byte)(unsafe.Pointer(&queryInfo))[:])
		copy(buf[infoSize:], name)

		return zx.ErrOk, nil, buf, nil
	case fdio.IoctlVFSGetDevicePath:
		return zx.ErrOk, nil, []byte(d.vfs.fs.DevicePath()), nil
	}
	return zx.ErrNotSupported, nil, nil, nil
}

func (d *directoryWrapper) Open(inFlags, inMode uint32, path string, object io.ObjectInterfaceRequest) error {
	flags := openFlagsFromFIDL(inFlags, inMode)
	if flags.Path() {
		flags &= fs.OpenFlagPath | fs.OpenFlagDirectory | fs.OpenFlagDescribe
	}
	fsFile, fsDir, fsRemote, err := d.dir.Open(path, flags)

	// Handle the case of a remote, and just forward.
	if fsRemote != nil {
		fwd := ((*io.DirectoryInterface)(&fidl.Proxy{Channel: fsRemote.Channel}))
		flags, mode := openFlagsToFIDL(fsRemote.Flags)
		if inFlags&io.KOpenFlagDescribe != 0 {
			flags |= io.KOpenFlagDescribe
		}
		return fwd.Open(flags, mode, fsRemote.Path, object)
	}

	// Handle the file and directory cases. They're mostly the same, except where noted.
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		var err error
		if fsFile != nil {
			_, err = d.vfs.addFile(fsFile, object)
		} else {
			_, err = d.vfs.addDirectory(fsDir, object)
		}
		if err != nil {
			return err
		}
	} else {
		// We got an error, so we want to make sure we close the channel.
		// However, if we were asked to Describe, we might still want to notify
		// about the status, so just close on exit.
		c := fidl.InterfaceRequest(object).Channel
		defer c.Close()
	}

	// Only send an OnOpen message if OpenFlagDescribe is set.
	if inFlags&io.KOpenFlagDescribe != 0 {
		var info *io.ObjectInfo
		if fsFile != nil {
			info = &io.ObjectInfo{
				ObjectInfoTag: io.ObjectInfoFile,
				File: io.FileObject{
					Event: zx.Event(zx.HandleInvalid),
				},
			}
		} else {
			info = &io.ObjectInfo{
				ObjectInfoTag: io.ObjectInfoDirectory,
			}
		}
		c := fidl.InterfaceRequest(object).Channel
		pxy := io.ObjectEventProxy(fidl.Proxy{Channel: c})
		return pxy.OnOpen(zxErr, info)
	}

	return nil
}

func (d *directoryWrapper) Unlink(path string) (zx.Status, error) {
	return errorToZx(d.dir.Unlink(path)), nil
}

func (d *directoryWrapper) ReadDirents(maxOut uint64) (zx.Status, []byte, error) {
	if maxOut > io.KMaxBuf {
		return zx.ErrInvalidArgs, nil, nil
	}
	if d.reading && len(d.dirents) == 0 {
		d.reading = false
		return zx.ErrOk, nil, nil
	}
	if !d.reading {
		dirents, err := d.dir.Read()
		if zxErr := errorToZx(err); zxErr != zx.ErrOk {
			return zxErr, nil, nil
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
		// Include the null character in the dirent name (BEFORE alignment)
		name := append([]byte(dirent.GetName()), 0)
		// The dirent size is rounded up to four bytes
		align := func(a int) int {
			return (a + 3) &^ 3
		}
		size := align(len(name)) + 8
		sysDirent.Size = uint32(size)
		sysDirent.Type = (fileTypeToFIDL(dirent.GetType()) >> 12) & 15
		if uint64(written+size) > maxOut {
			break
		}
		copy(bytes[written:], (*(*[8]byte)(unsafe.Pointer(&sysDirent)))[:])
		copy(bytes[written+8:], name)
		written += size
	}
	if i == len(d.dirents)-1 { // We finished reading the directory. Next readdir will be empty.
		d.dirents = nil
	} else { // Partial read
		d.dirents = d.dirents[i:]
	}
	d.reading = true
	return zx.ErrOk, bytes[:written], nil
}

func (d *directoryWrapper) Rewind() (zx.Status, error) {
	d.reading = false
	return zx.ErrOk, nil
}

func (d *directoryWrapper) GetToken() (zx.Status, zx.Handle, error) {
	d.vfs.Lock()
	defer d.vfs.Unlock()
	if d.e != 0 {
		if e, err := d.e.Duplicate(zx.RightSameRights); err != nil {
			return errorToZx(err), zx.HandleInvalid, nil
		} else {
			return zx.ErrOk, zx.Handle(e), nil
		}
	}

	// Create a new event which may later be used to refer to this object
	e0, err := zx.NewEvent(0)
	if err != nil {
		return errorToZx(err), zx.HandleInvalid, nil
	}

	d.e = e0

	// One handle to the event returns to the client, one end is kept on the
	// server (and is accessible within the cookie).
	var e1 zx.Event
	if e1, err = e0.Duplicate(zx.RightSameRights); err != nil {
		goto fail_event_created
	}
	if err := e0.Handle().SetCookie(zx.ProcHandle, uint64(d.token)); err != nil {
		goto fail_event_duplicated
	}
	return zx.ErrOk, zx.Handle(e1), nil

fail_event_duplicated:
	e1.Close()
fail_event_created:
	e0.Close()
	d.e = zx.Event(zx.HandleInvalid)
	return errorToZx(err), zx.HandleInvalid, nil
}

func (d *directoryWrapper) Rename(src string, token zx.Handle, dst string) (zx.Status, error) {
	if len(src) < 1 || len(dst) < 1 {
		return zx.ErrInvalidArgs, nil
	}
	d.vfs.Lock()
	defer d.vfs.Unlock()
	cookie, err := token.GetCookie(zx.ProcHandle)
	if err != nil {
		return zx.ErrInvalidArgs, nil
	}
	dir, ok := d.vfs.dirs[fidl.BindingKey(cookie)]
	if !ok {
		return zx.ErrInvalidArgs, nil
	}
	return errorToZx(d.dir.Rename(dir.dir, src, dst)), nil
}

func (d *directoryWrapper) Link(src string, token zx.Handle, dst string) (zx.Status, error) {
	return zx.ErrNotSupported, nil
}

type fileWrapper struct {
	vfs   *ThinVFS
	token fidl.BindingKey
	file  fs.File
}

func (f *fileWrapper) Clone(flags uint32, object io.ObjectInterfaceRequest) error {
	newFile, err := f.file.Dup()
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		_, err := f.vfs.addFile(newFile, object)
		if err != nil {
			return err
		}
	}
	// Only send an OnOpen message if OpenFlagDescribe is set.
	if flags&io.KOpenFlagDescribe != 0 {
		c := fidl.InterfaceRequest(object).Channel
		pxy := io.ObjectEventProxy(fidl.Proxy{Channel: c})
		return pxy.OnOpen(zx.ErrOk, &io.ObjectInfo{
			ObjectInfoTag: io.ObjectInfoFile,
			File: io.FileObject{
				Event: zx.Event(zx.HandleInvalid),
			},
		})
	}
	return nil
}

func (f *fileWrapper) Close() (zx.Status, error) {
	err := f.file.Close()

	f.vfs.Lock()
	defer f.vfs.Unlock()
	f.vfs.FileService.Remove(f.token)

	return errorToZx(err), nil
}

func (f *fileWrapper) ListInterfaces() ([]string, error) {
	return nil, nil
}

func (f *fileWrapper) Bind(iface string) error {
	return nil
}

func (f *fileWrapper) Describe() (io.ObjectInfo, error) {
	return io.ObjectInfo{
		ObjectInfoTag: io.ObjectInfoFile,
		File: io.FileObject{
			Event: zx.Event(zx.HandleInvalid),
		},
	}, nil
}

func (f *fileWrapper) Sync() (zx.Status, error) {
	return errorToZx(f.file.Sync()), nil
}

func (f *fileWrapper) GetAttr() (zx.Status, io.NodeAttributes, error) {
	size, _, mtime, err := f.file.Stat()
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return zxErr, io.NodeAttributes{}, nil
	}
	return zx.ErrOk, io.NodeAttributes{
		Mode:             syscall.S_IFREG | 0644,
		Id:               1,
		ContentSize:      uint64(size),
		StorageSize:      uint64(size),
		LinkCount:        1,
		CreationTime:     uint64(mtime.Unix()),
		ModificationTime: uint64(mtime.Unix()),
	}, nil
}

func (f *fileWrapper) SetAttr(flags uint32, attr io.NodeAttributes) (zx.Status, error) {
	if f.file.GetOpenFlags().Path() {
		return zx.ErrBadHandle, nil
	}
	t := time.Unix(0, int64(attr.ModificationTime))
	return errorToZx(f.file.Touch(t, t)), nil
}

func (f *fileWrapper) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (zx.Status, []zx.Handle, []uint8, error) {
	return zx.ErrNotSupported, nil, nil, nil
}

func (f *fileWrapper) Read(count uint64) (zx.Status, []uint8, error) {
	buf := make([]byte, count)
	r, err := f.file.Read(buf, 0, fs.WhenceFromCurrent)
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return zxErr, nil, nil
	}
	return zx.ErrOk, buf[:r], nil
}

func (f *fileWrapper) ReadAt(count, offset uint64) (zx.Status, []uint8, error) {
	buf := make([]byte, count)
	r, err := f.file.Read(buf, int64(offset), fs.WhenceFromStart)
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return zxErr, nil, nil
	}
	return zx.ErrOk, buf[:r], nil
}

func (f *fileWrapper) Write(data []uint8) (zx.Status, uint64, error) {
	r, err := f.file.Write(data, 0, fs.WhenceFromCurrent)
	return errorToZx(err), uint64(r), nil
}

func (f *fileWrapper) WriteAt(data []uint8, offset uint64) (zx.Status, uint64, error) {
	r, err := f.file.Write(data, int64(offset), fs.WhenceFromStart)
	return errorToZx(err), uint64(r), nil
}

func (f *fileWrapper) Seek(offset int64, start io.SeekOrigin) (zx.Status, uint64, error) {
	r, err := f.file.Seek(offset, int(start))
	return errorToZx(err), uint64(r), nil
}

func (f *fileWrapper) Truncate(length uint64) (zx.Status, error) {
	return errorToZx(f.file.Truncate(length)), nil
}

func (f *fileWrapper) GetFlags() (zx.Status, uint32, error) {
	oflags := uint32(f.file.GetOpenFlags())
	return zx.ErrOk, oflags & (rightFlags | statusFlags), nil
}

func (f *fileWrapper) SetFlags(inFlags uint32) (zx.Status, error) {
	flags := uint32(openFlagsFromFIDL(inFlags, 0))
	uflags := (uint32(f.file.GetOpenFlags()) & ^statusFlags) | (flags & statusFlags)
	return errorToZx(f.file.SetOpenFlags(fs.OpenFlags(uflags))), nil
}

func (f *fileWrapper) GetVmo(flags uint32) (zx.Status, zx.VMO, error) {
	// This will fail catastrophically because the VMO cannot actually
	// be invalid by the FIDL file.
	return zx.ErrNotSupported, zx.VMO(zx.HandleInvalid), nil
}

func (f *fileWrapper) GetVmoAt(flags uint32, offset, length uint64) (zx.Status, zx.VMO, error) {
	// This will fail catastrophically because the VMO cannot actually
	// be invalid by the FIDL file.
	return zx.ErrNotSupported, zx.VMO(zx.HandleInvalid), nil
}

// TODO(smklein): Calibrate thinfs flags with standard C library flags to make conversion smoother

func errorToZx(err error) zx.Status {
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

const alignedFlags = uint32(syscall.FdioAlignedFlags | syscall.FsRightReadable | syscall.FsRightWritable)

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
