// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package rpc

import (
	"errors"
	"fmt"
	"strings"
	"sync"
	"syscall"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/fs"

	"syscall/mx"
	"syscall/mx/mxio"
	"syscall/mx/mxio/dispatcher"
	"syscall/mx/mxio/rio"
)

type directoryWrapper struct {
	d       fs.Directory
	dirents []fs.Dirent
	reading bool
}

type ThinVFS struct {
	sync.Mutex
	dispatcher *dispatcher.Dispatcher
	fs         fs.FileSystem
	files      map[int64]interface{}
	nextCookie int64
}

var vfs *ThinVFS

// Creates a new VFS and dispatcher, mount it at a path, and begin accepting RIO message on it.
func StartServer(path string, filesys fs.FileSystem) error {
	vfs = &ThinVFS{
		files: make(map[int64]interface{}),
		fs:    filesys,
	}
	d, err := dispatcher.New(rio.Handler)
	if err != nil {
		println("Failed to create dispatcher")
		return err
	}

	h, err := devmgrConnect(path)
	if err != nil {
		println("Failed to connect to devmgr")
		return err
	}
	var serverHandler rio.ServerHandler = mxioServer
	cookie := vfs.allocateCookie(&directoryWrapper{d: filesys.RootDirectory()})
	if err := d.AddHandler(h, serverHandler, cookie); err != nil {
		h.Close()
		return err
	}
	vfs.dispatcher = d
	d.Serve()
	return nil
}

// Makes a new pipe, registering one end with the 'mxioServer', and returning the other end
func (vfs *ThinVFS) CreateHandle(obj interface{}) (mx.Handle, error) {
	h, err := mx.MsgPipeCreate(0)
	if err != nil {
		return 0, err
	}

	var serverHandler rio.ServerHandler = mxioServer
	if err := vfs.dispatcher.AddHandler(h[0], serverHandler, vfs.allocateCookie(obj)); err != nil {
		h[0].Close()
		h[1].Close()
		return 0, err
	}
	return h[1], nil
}

// TODO(smklein): Calibrate thinfs flags with standard C library flags to make conversion smoother

func errorToRIO(err error) mx.Status {
	switch err {
	case nil, fs.ErrEOF:
		// ErrEOF can be translated directly to ErrOk. For operations which return with an error if
		// partially complete (such as 'Read'), RemoteIO does not flag an error -- instead, it
		// simply returns the number of bytes which were processed.
		return mx.ErrOk
	case fs.ErrInvalidArgs:
		return mx.ErrInvalidArgs
	case fs.ErrNotFound:
		return mx.ErrNotFound
	case fs.ErrAlreadyExists:
		return mx.ErrAlreadyExists
	case fs.ErrPermission, fs.ErrReadOnly:
		return mx.ErrAccessDenied
	case fs.ErrResourceExhausted:
		return mx.ErrNoResources
	case fs.ErrFailedPrecondition, fs.ErrNotEmpty, fs.ErrNotOpen, fs.ErrIsActive, fs.ErrUnmounted:
		return mx.ErrBadState
	case fs.ErrNotAFile:
		return mx.ErrNotFile
	case fs.ErrNotADir:
		return mx.ErrNotDir
	default:
		return mx.ErrInternal
	}
}

func fileTypeToRIO(t fs.FileType) uint32 {
	switch t {
	case fs.FileTypeRegularFile:
		return syscall.S_IFREG
	case fs.FileTypeDirectory:
		return syscall.S_IFDIR
	default:
		return 0
	}
}

func openFlagsFromRIO(arg int32, mode uint32) fs.OpenFlags {
	res := fs.OpenFlags(0)

	// File access mode
	if arg&syscall.O_RDWR != 0 {
		res |= fs.OpenFlagRead | fs.OpenFlagWrite
	} else if arg&syscall.O_WRONLY != 0 {
		res |= fs.OpenFlagWrite
	} else {
		res |= fs.OpenFlagRead
	}

	// Additional open flags
	if arg&syscall.O_CREAT != 0 {
		res |= fs.OpenFlagCreate
		res |= fs.OpenFlagWrite | fs.OpenFlagRead
	}
	if arg&syscall.O_EXCL != 0 {
		res |= fs.OpenFlagExclusive
	}
	if arg&syscall.O_TRUNC != 0 {
		res |= fs.OpenFlagTruncate
	}
	if arg&syscall.O_APPEND != 0 {
		res |= fs.OpenFlagAppend
	}
	if arg&syscall.O_DIRECTORY != 0 {
		res |= fs.OpenFlagDirectory
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

func (vfs *ThinVFS) processOpFile(msg *rio.Msg, f fs.File, cookie int64) mx.Status {
	inputData := msg.Data[:msg.Datalen]
	msg.Datalen = 0
	switch msg.Op() {
	case rio.OpClone:
		f2, err := f.Dup()
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		h, err := vfs.CreateHandle(f2)
		if err != nil {
			return mx.ErrInternal
		}
		msg.SetProtocol(uint32(mxio.ProtocolRemote))
		msg.Handle[0] = h
		msg.Hcount = 1
		return mx.ErrOk
	case rio.OpClose:
		err := f.Close()
		vfs.freeCookie(cookie)
		return errorToRIO(err)
	case rio.OpRead:
		r, err := f.Read(msg.Data[:msg.Arg], 0, fs.WhenceFromCurrent)
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		msg.Datalen = uint32(r)
		return mx.Status(r)
	case rio.OpReadAt:
		r, err := f.Read(msg.Data[:msg.Arg], msg.Off(), fs.WhenceFromStart)
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		msg.Datalen = uint32(r)
		return mx.Status(r)
	case rio.OpWrite:
		r, err := f.Write(inputData, 0, fs.WhenceFromCurrent)
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		return mx.Status(r)
	case rio.OpWriteAt:
		r, err := f.Write(inputData, msg.Off(), fs.WhenceFromStart)
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		return mx.Status(r)
	case rio.OpSeek:
		r, err := f.Seek(msg.Off(), int(msg.Arg))
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		msg.SetOff(r)
		return mx.ErrOk
	case rio.OpStat:
		size, _, _, err := f.Stat()
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		return statShared(msg, size, false)
	case rio.OpTruncate:
		off := msg.Off()
		if off < 0 {
			return mx.ErrInvalidArgs
		}
		err := f.Truncate(uint64(off))
		return errorToRIO(err)
	default:
		println("ThinFS FILE UNKNOWN OP")
		return mx.ErrNotSupported
	}
	return mx.ErrNotSupported
}

func statShared(msg *rio.Msg, size int64, dir bool) mx.Status {
	r := syscall.Stat_t{}
	if dir {
		r.Dev = syscall.S_IFDIR
	} else {
		r.Dev = syscall.S_IFREG
	}
	r.Size = uint64(size)
	*(*syscall.Stat_t)(unsafe.Pointer(&msg.Data[0])) = r
	msg.Datalen = uint32(unsafe.Sizeof(r))
	return mx.Status(msg.Datalen)
}

func (vfs *ThinVFS) processOpDirectory(msg *rio.Msg, dw *directoryWrapper, cookie int64) mx.Status {
	inputData := msg.Data[:msg.Datalen]
	msg.Datalen = 0
	dir := dw.d
	switch msg.Op() {
	case rio.OpOpen:
		if len(inputData) < 1 {
			return mx.ErrInvalidArgs
		}
		path := strings.TrimRight(string(inputData), "\x00")
		flags := openFlagsFromRIO(msg.Arg, msg.Mode())
		f, d, err := dir.Open(path, flags)
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		var obj interface{}
		if f != nil {
			obj = f
		} else {
			obj = &directoryWrapper{d: d}
		}
		h, err := vfs.CreateHandle(obj)
		if err != nil {
			println("Failed to create a handle")
			if f != nil {
				f.Close()
			} else {
				d.Close()
			}
			vfs.freeCookie(cookie)
			return mx.ErrInternal
		}
		msg.SetProtocol(uint32(mxio.ProtocolRemote))
		msg.Hcount = 1
		msg.Handle[0] = h
		return mx.ErrOk
	case rio.OpClone:
		d2, err := dir.Dup()
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		h, err := vfs.CreateHandle(&directoryWrapper{d: d2})
		if err != nil {
			println("dir clone createhandle err: ", err.Error())
			return mx.ErrInternal
		}
		msg.SetProtocol(uint32(mxio.ProtocolRemote))
		msg.Handle[0] = h
		msg.Hcount = 1
		return mx.ErrOk
	case rio.OpClose:
		err := dir.Close()
		vfs.freeCookie(cookie)
		return errorToRIO(err)
	case rio.OpStat:
		size, _, _, err := dir.Stat()
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		return statShared(msg, size, true)
	case rio.OpReaddir:
		if dw.reading && len(dw.dirents) == 0 {
			// The final read of 'readdir' must return zero
			dw.reading = false
			return mx.Status(0)
		}
		if !dw.reading {
			dirents, err := dir.Read()
			if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
				return mxErr
			}
			dw.reading = true
			dw.dirents = dirents
		}
		bytesWritten := uint32(0)
		var i int
		for i = range dw.dirents {
			dirent := dw.dirents[i]
			rioDirent := syscall.Dirent{}
			// Include the null character in the dirent name (BEFORE alignment)
			name := append([]byte(dirent.GetName()), 0)
			// The dirent size is rounded up to four bytes
			align := func(a int) int {
				return (a + 3) &^ 3
			}
			rioDirent.Size = uint32(align(len(name))) + 8
			rioDirent.Type = fileTypeToRIO(dirent.GetType())
			if bytesWritten+rioDirent.Size > mxio.ChunkSize {
				break
			}
			//			*(*syscall.Dirent)(unsafe.Pointer(&msg.Data[bytesWritten])) = rioDirent
			copy(msg.Data[bytesWritten:], (*(*[8]byte)(unsafe.Pointer(&rioDirent)))[:])
			copy(msg.Data[bytesWritten+8:], name)
			bytesWritten += rioDirent.Size
		}
		if i == len(dw.dirents)-1 { // We finished reading the directory. Next readdir will be empty.
			dw.dirents = nil
		} else { // Partial read
			dw.dirents = dw.dirents[i:]
		}
		msg.Datalen = bytesWritten
		dw.reading = true
		return mx.Status(msg.Datalen)
	case rio.OpUnlink:
		path := strings.TrimRight(string(inputData), "\x00")
		err := dir.Unlink(path)
		msg.Datalen = 0
		return errorToRIO(err)
	case rio.OpRename:
		if len(inputData) < 4 { // Src + null + dst + null
			return mx.ErrInvalidArgs
		}
		paths := strings.Split(strings.TrimRight(string(inputData), "\x00"), "\x00")
		if len(paths) != 2 {
			return mx.ErrInvalidArgs
		}
		err := dir.Rename(paths[0], paths[1])
		msg.Datalen = 0
		return errorToRIO(err)
	case rio.OpIoctl:
		switch msg.IoctlOp() {
		case mxio.IoctlDevmgrUnmountFS:
			return errorToRIO(vfs.fs.Close())
		default:
			return mx.ErrNotSupported
		}
	default:
		println("ThinFS DIR UNKNOWN OP")
		return mx.ErrNotSupported
	}
	return mx.ErrNotSupported
}

func mxioServer(msg *rio.Msg, rh mx.Handle, cookie int64) mx.Status {
	// Discard any arriving handles
	for i := 0; i < int(msg.Hcount); i++ {
		msg.Handle[i].Close()
	}

	// Determine if the object we're acting on is a directory or a file
	vfs.Lock()
	obj := vfs.files[cookie]
	vfs.Unlock()
	switch obj := obj.(type) {
	default:
		fmt.Printf("cookie %d resulted in unexpected type %T\n", cookie, obj)
		return mx.ErrInternal
	case fs.File:
		return vfs.processOpFile(msg, obj, cookie)
	case *directoryWrapper:
		return vfs.processOpDirectory(msg, obj, cookie)
	}
	return mx.ErrNotSupported
}

// Uses a custom ioctl to receive a handle from a point in the filesystem hierarchy, acting as a
// mount.
func devmgrConnect(path string) (mx.Handle, error) {
	fd, err := syscall.Open(path, syscall.O_RDWR, 0666)
	if err != nil {
		println("Couldn't open path: ", err.Error())
		return 0, err
	}
	defer syscall.Close(fd)
	_, h, err := syscall.Ioctl(fd, mxio.IoctlDevmgrMountFS, nil)
	if err != nil {
		println("Couldn't call IOCTL: ", err.Error())
		return 0, err
	}
	if len(h) != 1 {
		println("Bad ioctl result: ", err.Error())
		return 0, errors.New("Unexpected number of handles from devmgr")
	}
	return mx.Handle(h[0]), nil
}

// Allocates a unique identifier which can be used to access information about a RIO object
func (vfs *ThinVFS) allocateCookie(obj interface{}) int64 {
	vfs.Lock()
	defer vfs.Unlock()
	for i := vfs.nextCookie; ; i++ {
		if _, ok := vfs.files[i]; !ok {
			vfs.files[i] = obj
			vfs.nextCookie = i + 1
			return i
		}
	}
}

func (vfs *ThinVFS) freeCookie(cookie int64) {
	vfs.Lock()
	defer vfs.Unlock()
	delete(vfs.files, cookie)
}
