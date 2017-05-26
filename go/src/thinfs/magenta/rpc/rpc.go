// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package rpc

import (
	"fmt"
	"os"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/fs"

	"syscall/mx"
	"syscall/mx/mxio"
	"syscall/mx/mxio/dispatcher"
	"syscall/mx/mxio/rio"
)

type directoryWrapper struct {
	d       fs.Directory
	dirents []fs.Dirent
	reading bool
	e       mx.Event
}

type ThinVFS struct {
	sync.Mutex
	dispatcher *dispatcher.Dispatcher
	fs         fs.FileSystem
	files      map[int64]interface{}
	nextCookie int64
}

var vfs *ThinVFS

// Creates a new VFS and dispatcher and begin accepting RIO message on it.
func StartServer(filesys fs.FileSystem, h mx.Handle) error {
	vfs = &ThinVFS{
		files: make(map[int64]interface{}),
		fs:    filesys,
	}
	d, err := dispatcher.New(rio.Handler)
	if err != nil {
		println("Failed to create dispatcher")
		return err
	}

	var serverHandler rio.ServerHandler = mxioServer
	cookie := vfs.allocateCookie(&directoryWrapper{d: filesys.RootDirectory()})
	if err := d.AddHandler(h, serverHandler, int64(cookie)); err != nil {
		h.Close()
		return err
	}
	vfs.dispatcher = d

	// We're ready to serve
	if err := h.SignalPeer(0, mx.SignalUser0); err != nil {
		h.Close()
		return err
	}

	d.Serve()
	return nil
}

// AddHandler uses the given handle and cookie as the primary mechanism to communicate with the VFS
// object, and returns any additional handles required to interact with the object.  (at the moment,
// no additional handles are supported).
func (vfs *ThinVFS) AddHandler(h mx.Handle, obj interface{}) error {
	var serverHandler rio.ServerHandler = mxioServer
	cookie := vfs.allocateCookie(obj)
	if err := vfs.dispatcher.AddHandler(h, serverHandler, int64(cookie)); err != nil {
		vfs.freeCookie(cookie)
		return err
	}

	return nil
}

func (dw *directoryWrapper) GetToken(cookie int64) (mx.Handle, error) {
	if dw.e != 0 {
		if e, err := dw.e.Duplicate(mx.RightSameRights); err != nil {
			return 0, err
		} else {
			return mx.Handle(e), nil
		}
	}

	// Create a new event which may later be used to refer to this object
	e0, err := mx.NewEvent(0)
	if err != nil {
		return 0, err
	}

	dw.e = e0

	// One handle to the event returns to the client, one end is kept on the
	// server (and is accessible within the cookie).
	var e1 mx.Event
	if e1, err = e0.Duplicate(mx.RightSameRights); err != nil {
		goto fail_event_created
	}
	if err := mx.Handle(e0).SetCookie(mx.ProcHandle, uint64(cookie)); err != nil {
		goto fail_event_duplicated
	}
	return mx.Handle(e1), nil

fail_event_duplicated:
	e1.Close()
fail_event_created:
	e0.Close()
	dw.e = 0
	return 0, err
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
	case fs.ErrNoSpace:
		return mx.ErrNoSpace
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
			return rio.IndirectError(msg.Handle[0], mxErr)
		}
		ro := &rio.RioObject{
			RioObjectHeader: rio.RioObjectHeader{
				Status: mx.ErrOk,
				Type:   uint32(mxio.ProtocolRemote),
			},
			Esize:  0,
			Hcount: 0,
		}
		ro.Write(msg.Handle[0], 0)
		if err := vfs.AddHandler(msg.Handle[0], f2); err != nil {
			f2.Close()
		}
		return dispatcher.ErrIndirect.Status
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
		size, _, mtime, err := f.Stat()
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		return statShared(msg, size, mtime, false)
	case rio.OpTruncate:
		off := msg.Off()
		if off < 0 {
			return mx.ErrInvalidArgs
		}
		err := f.Truncate(uint64(off))
		return errorToRIO(err)
	case rio.OpSync:
		return errorToRIO(f.Sync())
	case rio.OpSetAttr:
		atime, mtime := getTimeShared(msg)
		return errorToRIO(f.Touch(atime, mtime))
	default:
		println("ThinFS FILE UNKNOWN OP: ", msg.Op())
		msg.DiscardHandles()
		return mx.ErrNotSupported
	}
	return mx.ErrNotSupported
}

func getTimeShared(msg *rio.Msg) (time.Time, time.Time) {
	var mtime time.Time
	attr := *(*mxio.Vnattr)(unsafe.Pointer(&msg.Data[0]))
	if (attr.Valid & mxio.AttrMtime) != 0 {
		mtime = time.Unix(0, int64(attr.ModifyTime))
	}
	return time.Time{}, mtime
}

func statShared(msg *rio.Msg, size int64, mtime time.Time, dir bool) mx.Status {
	r := mxio.Vnattr{}
	if dir {
		r.Mode = syscall.S_IFDIR
	} else {
		r.Mode = syscall.S_IFREG
	}
	r.Size = uint64(size)
	r.Nlink = 1
	r.ModifyTime = uint64(mtime.UnixNano())
	r.CreateTime = r.ModifyTime
	*(*mxio.Vnattr)(unsafe.Pointer(&msg.Data[0])) = r
	msg.Datalen = uint32(unsafe.Sizeof(r))
	return mx.Status(msg.Datalen)
}

func (vfs *ThinVFS) processOpDirectory(msg *rio.Msg, rh mx.Handle, dw *directoryWrapper, cookie int64) mx.Status {
	inputData := msg.Data[:msg.Datalen]
	msg.Datalen = 0
	dir := dw.d
	switch msg.Op() {
	case rio.OpOpen:
		if len(inputData) < 1 {
			return rio.IndirectError(msg.Handle[0], mx.ErrInvalidArgs)
		}
		path := strings.TrimRight(string(inputData), "\x00")
		flags := openFlagsFromRIO(msg.Arg, msg.Mode())
		f, d, err := dir.Open(path, flags)
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return rio.IndirectError(msg.Handle[0], mxErr)
		}
		var obj interface{}
		if f != nil {
			obj = f
		} else {
			obj = &directoryWrapper{d: d}
		}

		ro := &rio.RioObject{
			RioObjectHeader: rio.RioObjectHeader{
				Status: mx.ErrOk,
				Type:   uint32(mxio.ProtocolRemote),
			},
			Esize:  0,
			Hcount: 0,
		}
		ro.Write(msg.Handle[0], 0)
		if err := vfs.AddHandler(msg.Handle[0], obj); err != nil {
			println("Failed to create a handle")
			if f != nil {
				f.Close()
			} else {
				d.Close()
			}
		}
		return dispatcher.ErrIndirect.Status
	case rio.OpClone:
		d2, err := dir.Dup()
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return rio.IndirectError(msg.Handle[0], mxErr)
		}
		ro := &rio.RioObject{
			RioObjectHeader: rio.RioObjectHeader{
				Status: mx.ErrOk,
				Type:   uint32(mxio.ProtocolRemote),
			},
			Esize:  0,
			Hcount: 0,
		}
		ro.Write(msg.Handle[0], 0)
		if err := vfs.AddHandler(msg.Handle[0], &directoryWrapper{d: d2}); err != nil {
			d2.Close()
		}
		return dispatcher.ErrIndirect.Status
	case rio.OpClose:
		err := dir.Close()
		vfs.Lock()
		if dw.e != 0 {
			mx.Handle(dw.e).SetCookie(mx.ProcHandle, 0)
		}
		vfs.Unlock()
		vfs.freeCookie(cookie)
		return errorToRIO(err)
	case rio.OpStat:
		size, _, mtime, err := dir.Stat()
		if mxErr := errorToRIO(err); mxErr != mx.ErrOk {
			return mxErr
		}
		return statShared(msg, size, mtime, true)
	case rio.OpReaddir:
		maxlen := uint32(msg.Arg)
		if maxlen > mxio.ChunkSize {
			return mx.ErrInvalidArgs
		}
		if msg.Off() == 1 {
			// TODO(smklein): 1 == ReaddirCmdReset; update the Go standard library to include this
			// magic number.
			dw.reading = false
		}
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
			rioDirent.Type = (fileTypeToRIO(dirent.GetType()) >> 12) & 15
			if bytesWritten+rioDirent.Size > maxlen {
				break
			}
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
		defer msg.DiscardHandles()
		if len(inputData) < 4 { // Src + null + dst + null
			return mx.ErrInvalidArgs
		}
		paths := strings.Split(strings.TrimRight(string(inputData), "\x00"), "\x00")
		if len(paths) != 2 {
			return mx.ErrInvalidArgs
		}

		vfs.Lock()
		defer vfs.Unlock()
		cookie, err := msg.Handle[0].GetCookie(mx.ProcHandle)
		if err != nil {
			return mx.ErrInvalidArgs
		}
		obj := vfs.files[int64(cookie)]
		switch obj := obj.(type) {
		case *directoryWrapper:
			return errorToRIO(dir.Rename(obj.d, paths[0], paths[1]))
		default:
			return mx.ErrInvalidArgs
		}
	case rio.OpSync:
		return errorToRIO(dir.Sync())
	case rio.OpIoctl:
		switch msg.IoctlOp() {
		case mxio.IoctlDevmgrGetTokenFS:
			vfs.Lock()
			defer vfs.Unlock()
			h, err := dw.GetToken(cookie)
			if err != nil {
				return errorToRIO(err)
			}
			msg.Handle[0] = h
			msg.Hcount = 1
			return mx.ErrOk
		case mxio.IoctlDevmgrUnmountFS:
			// Shut down filesystem
			err := vfs.fs.Close()
			if err != nil {
				println("Error closing FAT: ", err.Error())
			}
			// Close reply handle, indicating that the unmounting process is complete
			rh.Close()
			os.Exit(0)
		default:
			return mx.ErrNotSupported
		}
	case rio.OpSetAttr:
		atime, mtime := getTimeShared(msg)
		return errorToRIO(dir.Touch(atime, mtime))
	default:
		println("ThinFS DIR UNKNOWN OP: ", msg.Op())
		msg.DiscardHandles()
		return mx.ErrNotSupported
	}
	return mx.ErrNotSupported
}

func mxioServer(msg *rio.Msg, rh mx.Handle, cookie int64) mx.Status {
	if msg.Hcount != msg.OpHandleCount() {
		// Incoming number of handles must match message type
		msg.DiscardHandles()
		return mx.ErrIO
	}

	// Determine if the object we're acting on is a directory or a file
	vfs.Lock()
	obj := vfs.files[int64(cookie)]
	vfs.Unlock()
	if obj == nil && msg.Op() == rio.OpClose {
		// Removing object that has already been removed
		return mx.ErrOk
	}
	switch obj := obj.(type) {
	case fs.File:
		return vfs.processOpFile(msg, obj, cookie)
	case *directoryWrapper:
		return vfs.processOpDirectory(msg, rh, obj, cookie)
	default:
		fmt.Printf("cookie %d resulted in unexpected type %T\n", cookie, obj)
		msg.DiscardHandles()
		return mx.ErrInternal
	}
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
