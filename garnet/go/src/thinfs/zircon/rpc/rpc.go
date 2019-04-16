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
	"syscall/zx/fidl"
	"syscall/zx/io"
	"syscall/zx/mem"
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
	ireq := io.NodeInterfaceRequest(fidl.InterfaceRequest{Channel: zx.Channel(h)})
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

func (vfs *ThinVFS) addDirectory(dir fs.Directory, node io.NodeInterfaceRequest) (fidl.BindingKey, error) {
	d := &directoryWrapper{vfs: vfs, dir: dir}

	vfs.Lock()
	defer vfs.Unlock()
	tok, err := d.vfs.DirectoryService.Add(
		d,
		(fidl.InterfaceRequest(node)).Channel,
		func(err error) {
			vfs.Lock()
			defer vfs.Unlock()
			delete(vfs.dirs, d.token)
		},
	)
	if err != nil {
		return 0, err
	}
	d.token = tok
	vfs.dirs[tok] = d
	return tok, nil
}

func (vfs *ThinVFS) addFile(file fs.File, node io.NodeInterfaceRequest) (fidl.BindingKey, error) {
	f := &fileWrapper{vfs: vfs, file: file}

	vfs.Lock()
	defer vfs.Unlock()
	tok, err := vfs.FileService.Add(
		f,
		(fidl.InterfaceRequest(node)).Channel,
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

func (d *directoryWrapper) Clone(flags uint32, node io.NodeInterfaceRequest) error {
	newDir, err := d.dir.Dup()
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		_, err := d.vfs.addDirectory(newDir, node)
		if err != nil {
			return err
		}
	}
	// Only send an OnOpen message if OpenFlagDescribe is set.
	if flags&io.OpenFlagDescribe != 0 {
		c := fidl.InterfaceRequest(node).Channel
		pxy := io.NodeEventProxy(fidl.ChannelProxy{Channel: c})
		info := &io.NodeInfo{
			NodeInfoTag: io.NodeInfoDirectory,
		}
		return pxy.OnOpen(int32(zxErr), info)
	}
	return nil
}

func (d *directoryWrapper) Close() (int32, error) {
	err := d.dir.Close()

	d.vfs.Lock()
	defer d.vfs.Unlock()
	d.clearCookie()
	d.vfs.DirectoryService.Remove(d.token)
	delete(d.vfs.dirs, d.token)

	return int32(errorToZx(err)), nil
}

func (d *directoryWrapper) ListInterfaces() ([]string, error) {
	return nil, nil
}

func (d *directoryWrapper) Describe() (io.NodeInfo, error) {
	return io.NodeInfo{
		NodeInfoTag: io.NodeInfoDirectory,
	}, nil
}

func (d *directoryWrapper) Sync() (int32, error) {
	return int32(errorToZx(d.dir.Sync())), nil
}

func (d *directoryWrapper) GetAttr() (int32, io.NodeAttributes, error) {
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

func (d *directoryWrapper) SetAttr(flags uint32, attr io.NodeAttributes) (int32, error) {
	t := time.Unix(0, int64(attr.ModificationTime))
	return int32(errorToZx(d.dir.Touch(t, t))), nil
}

func (d *directoryWrapper) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	return int32(zx.ErrNotSupported), nil, nil, nil
}

func (d *directoryWrapper) Open(inFlags, inMode uint32, path string, node io.NodeInterfaceRequest) error {
	flags := openFlagsFromFIDL(inFlags, inMode)
	if flags.Path() {
		flags &= fs.OpenFlagPath | fs.OpenFlagDirectory | fs.OpenFlagDescribe
	}
	fsFile, fsDir, fsRemote, err := d.dir.Open(path, flags)

	// Handle the case of a remote, and just forward.
	if fsRemote != nil {
		fwd := ((*io.DirectoryInterface)(&fidl.ChannelProxy{Channel: fsRemote.Channel}))
		flags, mode := openFlagsToFIDL(fsRemote.Flags)
		if inFlags&io.OpenFlagDescribe != 0 {
			flags |= io.OpenFlagDescribe
		}
		return fwd.Open(flags, mode, fsRemote.Path, node)
	}

	// Handle the file and directory cases. They're mostly the same, except where noted.
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		var err error
		if fsFile != nil {
			_, err = d.vfs.addFile(fsFile, node)
		} else {
			_, err = d.vfs.addDirectory(fsDir, node)
		}
		if err != nil {
			return err
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
		var info *io.NodeInfo
		if fsFile != nil {
			info = &io.NodeInfo{
				NodeInfoTag: io.NodeInfoFile,
				File: io.FileObject{
					Event: zx.Event(zx.HandleInvalid),
				},
			}
		} else {
			info = &io.NodeInfo{
				NodeInfoTag: io.NodeInfoDirectory,
			}
		}
		c := fidl.InterfaceRequest(node).Channel
		pxy := io.NodeEventProxy(fidl.ChannelProxy{Channel: c})
		return pxy.OnOpen(int32(zxErr), info)
	}

	return nil
}

func (d *directoryWrapper) Unlink(path string) (int32, error) {
	return int32(errorToZx(d.dir.Unlink(path))), nil
}

const direntSize = int(unsafe.Offsetof(syscall.Dirent{}.Name))

func (d *directoryWrapper) ReadDirents(maxOut uint64) (int32, []byte, error) {
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

func (d *directoryWrapper) Rewind() (int32, error) {
	d.reading = false
	return int32(zx.ErrOk), nil
}

func (d *directoryWrapper) GetToken() (int32, zx.Handle, error) {
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

func (d *directoryWrapper) Rename(src string, token zx.Handle, dst string) (int32, error) {
	if len(src) < 1 || len(dst) < 1 {
		return int32(zx.ErrInvalidArgs), nil
	}
	d.vfs.Lock()
	defer d.vfs.Unlock()
	cookie := d.getCookie(token)
	if cookie == 0 {
		return int32(zx.ErrInvalidArgs), nil
	}
	dir, ok := d.vfs.dirs[fidl.BindingKey(cookie)]
	if !ok {
		return int32(zx.ErrInvalidArgs), nil
	}
	return int32(errorToZx(d.dir.Rename(dir.dir, src, dst))), nil
}

func (d *directoryWrapper) Link(src string, token zx.Handle, dst string) (int32, error) {
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) Watch(mask uint32, options uint32, watcher zx.Channel) (int32, error) {
	watcher.Close()
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) Mount(remote io.DirectoryInterfaceRequest) (int32, error) {
	remote.Close()
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) MountAndCreate(remote io.DirectoryInterfaceRequest, name string, flags uint32) (int32, error) {
	remote.Close()
	return int32(zx.ErrNotSupported), nil
}

func (d *directoryWrapper) Unmount() (int32, error) {
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
	return int32(zx.ErrOk), nil
}

func (d *directoryWrapper) UnmountNode() (int32, io.DirectoryInterfaceRequest, error) {
	return int32(zx.ErrNotSupported), io.DirectoryInterfaceRequest(fidl.InterfaceRequest{Channel: zx.Channel(zx.HandleInvalid)}), nil
}

func (d *directoryWrapper) QueryFilesystem() (int32, *io.FilesystemInfo, error) {
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

type fileWrapper struct {
	vfs   *ThinVFS
	token fidl.BindingKey
	file  fs.File
}

func (f *fileWrapper) Clone(flags uint32, node io.NodeInterfaceRequest) error {
	newFile, err := f.file.Dup()
	zxErr := errorToZx(err)
	if zxErr == zx.ErrOk {
		_, err := f.vfs.addFile(newFile, node)
		if err != nil {
			return err
		}
	}
	// Only send an OnOpen message if OpenFlagDescribe is set.
	if flags&io.OpenFlagDescribe != 0 {
		c := fidl.InterfaceRequest(node).Channel
		pxy := io.NodeEventProxy(fidl.ChannelProxy{Channel: c})
		return pxy.OnOpen(int32(zx.ErrOk), &io.NodeInfo{
			NodeInfoTag: io.NodeInfoFile,
			File: io.FileObject{
				Event: zx.Event(zx.HandleInvalid),
			},
		})
	}
	return nil
}

func (f *fileWrapper) Close() (int32, error) {
	err := f.file.Close()

	f.vfs.Lock()
	defer f.vfs.Unlock()
	f.vfs.FileService.Remove(f.token)

	return int32(errorToZx(err)), nil
}

func (f *fileWrapper) ListInterfaces() ([]string, error) {
	return nil, nil
}

func (f *fileWrapper) Describe() (io.NodeInfo, error) {
	return io.NodeInfo{
		NodeInfoTag: io.NodeInfoFile,
		File: io.FileObject{
			Event: zx.Event(zx.HandleInvalid),
		},
	}, nil
}

func (f *fileWrapper) Sync() (int32, error) {
	return int32(errorToZx(f.file.Sync())), nil
}

func (f *fileWrapper) GetAttr() (int32, io.NodeAttributes, error) {
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

func (f *fileWrapper) SetAttr(flags uint32, attr io.NodeAttributes) (int32, error) {
	if f.file.GetOpenFlags().Path() {
		return int32(zx.ErrBadHandle), nil
	}
	t := time.Unix(0, int64(attr.ModificationTime))
	return int32(errorToZx(f.file.Touch(t, t))), nil
}

func (f *fileWrapper) Ioctl(opcode uint32, maxOut uint64, handles []zx.Handle, in []uint8) (int32, []zx.Handle, []uint8, error) {
	return int32(zx.ErrNotSupported), nil, nil, nil
}

func (f *fileWrapper) Read(count uint64) (int32, []uint8, error) {
	buf := make([]byte, count)
	r, err := f.file.Read(buf, 0, fs.WhenceFromCurrent)
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return int32(zxErr), nil, nil
	}
	return int32(zx.ErrOk), buf[:r], nil
}

func (f *fileWrapper) ReadAt(count, offset uint64) (int32, []uint8, error) {
	buf := make([]byte, count)
	r, err := f.file.Read(buf, int64(offset), fs.WhenceFromStart)
	if zxErr := errorToZx(err); zxErr != zx.ErrOk {
		return int32(zxErr), nil, nil
	}
	return int32(zx.ErrOk), buf[:r], nil
}

func (f *fileWrapper) Write(data []uint8) (int32, uint64, error) {
	r, err := f.file.Write(data, 0, fs.WhenceFromCurrent)
	return int32(errorToZx(err)), uint64(r), nil
}

func (f *fileWrapper) WriteAt(data []uint8, offset uint64) (int32, uint64, error) {
	r, err := f.file.Write(data, int64(offset), fs.WhenceFromStart)
	return int32(errorToZx(err)), uint64(r), nil
}

func (f *fileWrapper) Seek(offset int64, start io.SeekOrigin) (int32, uint64, error) {
	r, err := f.file.Seek(offset, int(start))
	return int32(errorToZx(err)), uint64(r), nil
}

func (f *fileWrapper) Truncate(length uint64) (int32, error) {
	return int32(errorToZx(f.file.Truncate(length))), nil
}

func (f *fileWrapper) GetFlags() (int32, uint32, error) {
	oflags := uint32(f.file.GetOpenFlags())
	return int32(zx.ErrOk), oflags & (rightFlags | statusFlags), nil
}

func (f *fileWrapper) SetFlags(inFlags uint32) (int32, error) {
	flags := uint32(openFlagsFromFIDL(inFlags, 0))
	uflags := (uint32(f.file.GetOpenFlags()) & ^statusFlags) | (flags & statusFlags)
	return int32(errorToZx(f.file.SetOpenFlags(fs.OpenFlags(uflags)))), nil
}

func (f *fileWrapper) GetVmo(flags uint32) (int32, zx.VMO, error) {
	return int32(zx.ErrNotSupported), zx.VMO(zx.HandleInvalid), nil
}

func (f *fileWrapper) GetBuffer(flags uint32) (int32, *mem.Buffer, error) {
	return int32(zx.ErrNotSupported), nil, nil
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
