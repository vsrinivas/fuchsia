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

	"thinfs/fs"

	"syscall/zx"
	"syscall/zx/fdio"
)

type directoryWrapper struct {
	d       fs.Directory
	dirents []fs.Dirent
	reading bool
	e       zx.Event
}

type ThinVFS struct {
	sync.Mutex
	dispatcher *fdio.Dispatcher
	fs         fs.FileSystem
	files      map[int64]interface{}
	nextCookie int64
}

type VfsQueryInfo struct {
	TotalBytes uint64
	UsedBytes  uint64
	TotalNodes uint64
	UsedNodes  uint64
}

// NewServer creates a new ThinVFS server. Serve must be called to begin servicing the filesystem.
func NewServer(filesys fs.FileSystem, h zx.Handle) (*ThinVFS, error) {
	vfs := &ThinVFS{
		files: make(map[int64]interface{}),
		fs:    filesys,
	}
	d, err := fdio.NewDispatcher(fdio.Handler)
	if err != nil {
		println("Failed to create dispatcher")
		return vfs, err
	}

	var serverHandler fdio.ServerHandler = vfs.fdioServer
	cookie := vfs.allocateCookie(&directoryWrapper{d: filesys.RootDirectory()})
	if err := d.AddHandler(h, serverHandler, int64(cookie)); err != nil {
		h.Close()
		return vfs, err
	}
	vfs.dispatcher = d

	// We're ready to serve
	if err := h.SignalPeer(0, zx.SignalUser0); err != nil {
		h.Close()
		return vfs, err
	}

	return vfs, nil
}

// Serve begins dispatching rio requests. Serve blocks, so callers will normally want to run it in a new goroutine.
func (vfs *ThinVFS) Serve() {
	vfs.dispatcher.Serve()
}

// AddHandler uses the given handle and cookie as the primary mechanism to communicate with the VFS
// object, and returns any additional handles required to interact with the object.  (at the moment,
// no additional handles are supported).
func (vfs *ThinVFS) AddHandler(h zx.Handle, obj interface{}) error {
	var serverHandler fdio.ServerHandler = vfs.fdioServer
	cookie := vfs.allocateCookie(obj)
	if err := vfs.dispatcher.AddHandler(h, serverHandler, int64(cookie)); err != nil {
		vfs.freeCookie(cookie)
		return err
	}

	return nil
}

func (dw *directoryWrapper) GetToken(cookie int64) (zx.Handle, error) {
	if dw.e != 0 {
		if e, err := dw.e.Duplicate(zx.RightSameRights); err != nil {
			return 0, err
		} else {
			return zx.Handle(e), nil
		}
	}

	// Create a new event which may later be used to refer to this object
	e0, err := zx.NewEvent(0)
	if err != nil {
		return 0, err
	}

	dw.e = e0

	// One handle to the event returns to the client, one end is kept on the
	// server (and is accessible within the cookie).
	var e1 zx.Event
	if e1, err = e0.Duplicate(zx.RightSameRights); err != nil {
		goto fail_event_created
	}
	if err := zx.Handle(e0).SetCookie(zx.ProcHandle, uint64(cookie)); err != nil {
		goto fail_event_duplicated
	}
	return zx.Handle(e1), nil

fail_event_duplicated:
	e1.Close()
fail_event_created:
	e0.Close()
	dw.e = 0
	return 0, err
}

// TODO(smklein): Calibrate thinfs flags with standard C library flags to make conversion smoother

func errorToRIO(err error) zx.Status {
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
	if arg&syscall.O_PIPELINE != 0 {
		res |= fs.OpenFlagPipeline
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

func (vfs *ThinVFS) processOpFile(msg *fdio.Msg, f fs.File, cookie int64) zx.Status {
	inputData := msg.Data[:msg.Datalen]
	msg.Datalen = 0
	switch msg.Op() {
	case fdio.OpClone:
		f2, err := f.Dup()
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return fdio.IndirectError(msg.Handle[0], mxErr)
		}
		ro := &fdio.RioObject{
			RioObjectHeader: fdio.RioObjectHeader{
				Status: zx.ErrOk,
				Type:   uint32(fdio.ProtocolRemote),
			},
			Esize:  0,
			Hcount: 0,
		}
		ro.Write(msg.Handle[0], 0)
		if err := vfs.AddHandler(msg.Handle[0], f2); err != nil {
			f2.Close()
		}
		return fdio.ErrIndirect.Status
	case fdio.OpClose:
		err := f.Close()
		vfs.freeCookie(cookie)
		return errorToRIO(err)
	case fdio.OpRead:
		r, err := f.Read(msg.Data[:msg.Arg], 0, fs.WhenceFromCurrent)
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return mxErr
		}
		msg.Datalen = uint32(r)
		return zx.Status(r)
	case fdio.OpReadAt:
		r, err := f.Read(msg.Data[:msg.Arg], msg.Off(), fs.WhenceFromStart)
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return mxErr
		}
		msg.Datalen = uint32(r)
		return zx.Status(r)
	case fdio.OpWrite:
		r, err := f.Write(inputData, 0, fs.WhenceFromCurrent)
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return mxErr
		}
		return zx.Status(r)
	case fdio.OpWriteAt:
		r, err := f.Write(inputData, msg.Off(), fs.WhenceFromStart)
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return mxErr
		}
		return zx.Status(r)
	case fdio.OpSeek:
		r, err := f.Seek(msg.Off(), int(msg.Arg))
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return mxErr
		}
		msg.SetOff(r)
		return zx.ErrOk
	case fdio.OpStat:
		size, _, mtime, err := f.Stat()
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return mxErr
		}
		return statShared(msg, size, mtime, false)
	case fdio.OpTruncate:
		off := msg.Off()
		if off < 0 {
			return zx.ErrInvalidArgs
		}
		err := f.Truncate(uint64(off))
		return errorToRIO(err)
	case fdio.OpSync:
		return errorToRIO(f.Sync())
	case fdio.OpSetAttr:
		atime, mtime := getTimeShared(msg)
		return errorToRIO(f.Touch(atime, mtime))
	case fdio.OpFcntl:
		flags := openFlagsFromRIO(int32(msg.FcntlFlags()), 0)
		statusFlags := fs.OpenFlagAppend
		switch uint32(msg.Arg) {
		case fdio.OpFcntlCmdGetFL:
			var cflags uint32
			oflags := f.GetOpenFlags()
			if oflags.Append() {
				cflags |= syscall.O_APPEND
			}
			if oflags.Read() && oflags.Write() {
				cflags |= syscall.O_RDWR
			} else if oflags.Write() {
				cflags |= syscall.O_WRONLY
			}
			msg.SetFcntlFlags(cflags)
			return zx.ErrOk
		case fdio.OpFcntlCmdSetFL:
			err := f.SetOpenFlags((f.GetOpenFlags() & ^statusFlags) | (flags & statusFlags))
			return errorToRIO(err)
		default:
			msg.DiscardHandles()
			return zx.ErrNotSupported
		}
	default:
		println("ThinFS FILE UNKNOWN OP: ", msg.Op())
		msg.DiscardHandles()
		return zx.ErrNotSupported
	}
	return zx.ErrNotSupported
}

func getTimeShared(msg *fdio.Msg) (time.Time, time.Time) {
	var mtime time.Time
	attr := *(*fdio.Vnattr)(unsafe.Pointer(&msg.Data[0]))
	if (attr.Valid & fdio.AttrMtime) != 0 {
		mtime = time.Unix(0, int64(attr.ModifyTime))
	}
	return time.Time{}, mtime
}

const pageSize = 4096

func statShared(msg *fdio.Msg, size int64, mtime time.Time, dir bool) zx.Status {
	r := fdio.Vnattr{}
	if dir {
		r.Mode = syscall.S_IFDIR
	} else {
		r.Mode = syscall.S_IFREG
	}
	r.Size = uint64(size)
	// "Blksize" and "Blkcount" are a bit of a lie at the moment, but
	// they should present realistic-looking values.
	// TODO(smklein): Plumb actual values through from the underlying filesystem.
	r.Blksize = pageSize
	r.Blkcount = (r.Size + fdio.VnattrBlksize - 1) / fdio.VnattrBlksize
	r.Nlink = 1
	r.ModifyTime = uint64(mtime.UnixNano())
	r.CreateTime = r.ModifyTime
	*(*fdio.Vnattr)(unsafe.Pointer(&msg.Data[0])) = r
	msg.Datalen = uint32(unsafe.Sizeof(r))
	return zx.Status(msg.Datalen)
}

func (vfs *ThinVFS) processOpDirectory(msg *fdio.Msg, rh zx.Handle, dw *directoryWrapper, cookie int64) zx.Status {
	inputData := msg.Data[:msg.Datalen]
	msg.Datalen = 0
	dir := dw.d
	switch msg.Op() {
	case fdio.OpOpen:
		if len(inputData) < 1 {
			return fdio.IndirectError(msg.Handle[0], zx.ErrInvalidArgs)
		}
		path := strings.TrimRight(string(inputData), "\x00")
		flags := openFlagsFromRIO(msg.Arg, msg.Mode())
		f, d, err := dir.Open(path, flags)
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return fdio.IndirectError(msg.Handle[0], mxErr)
		}
		var obj interface{}
		if f != nil {
			obj = f
		} else {
			obj = &directoryWrapper{d: d}
		}

		if !flags.Pipeline() {
			ro := &fdio.RioObject{
				RioObjectHeader: fdio.RioObjectHeader{
					Status: zx.ErrOk,
					Type:   uint32(fdio.ProtocolRemote),
				},
				Esize:  0,
				Hcount: 0,
			}
			ro.Write(msg.Handle[0], 0)
		}
		if err := vfs.AddHandler(msg.Handle[0], obj); err != nil {
			println("Failed to create a handle")
			if f != nil {
				f.Close()
			} else {
				d.Close()
			}
		}
		return fdio.ErrIndirect.Status
	case fdio.OpClone:
		d2, err := dir.Dup()
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return fdio.IndirectError(msg.Handle[0], mxErr)
		}
		ro := &fdio.RioObject{
			RioObjectHeader: fdio.RioObjectHeader{
				Status: zx.ErrOk,
				Type:   uint32(fdio.ProtocolRemote),
			},
			Esize:  0,
			Hcount: 0,
		}
		ro.Write(msg.Handle[0], 0)
		if err := vfs.AddHandler(msg.Handle[0], &directoryWrapper{d: d2}); err != nil {
			d2.Close()
		}
		return fdio.ErrIndirect.Status
	case fdio.OpClose:
		err := dir.Close()
		vfs.Lock()
		if dw.e != 0 {
			zx.Handle(dw.e).SetCookie(zx.ProcHandle, 0)
		}
		vfs.Unlock()
		vfs.freeCookie(cookie)
		return errorToRIO(err)
	case fdio.OpStat:
		size, _, mtime, err := dir.Stat()
		if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
			return mxErr
		}
		return statShared(msg, size, mtime, true)
	case fdio.OpReaddir:
		maxlen := uint32(msg.Arg)
		if maxlen > fdio.ChunkSize {
			return zx.ErrInvalidArgs
		}
		if msg.Off() == 1 {
			// TODO(smklein): 1 == ReaddirCmdReset; update the Go standard library to include this
			// magic number.
			dw.reading = false
		}
		if dw.reading && len(dw.dirents) == 0 {
			// The final read of 'readdir' must return zero
			dw.reading = false
			return zx.Status(0)
		}
		if !dw.reading {
			dirents, err := dir.Read()
			if mxErr := errorToRIO(err); mxErr != zx.ErrOk {
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
		return zx.Status(msg.Datalen)
	case fdio.OpUnlink:
		path := strings.TrimRight(string(inputData), "\x00")
		err := dir.Unlink(path)
		msg.Datalen = 0
		return errorToRIO(err)
	case fdio.OpRename:
		defer msg.DiscardHandles()
		if len(inputData) < 4 { // Src + null + dst + null
			return zx.ErrInvalidArgs
		}
		paths := strings.Split(strings.TrimRight(string(inputData), "\x00"), "\x00")
		if len(paths) != 2 {
			return zx.ErrInvalidArgs
		}

		vfs.Lock()
		defer vfs.Unlock()
		cookie, err := msg.Handle[0].GetCookie(zx.ProcHandle)
		if err != nil {
			return zx.ErrInvalidArgs
		}
		obj := vfs.files[int64(cookie)]
		switch obj := obj.(type) {
		case *directoryWrapper:
			return errorToRIO(dir.Rename(obj.d, paths[0], paths[1]))
		default:
			return zx.ErrInvalidArgs
		}
	case fdio.OpSync:
		return errorToRIO(dir.Sync())
	case fdio.OpIoctl:
		switch msg.IoctlOp() {
		case fdio.IoctlVFSGetTokenFS:
			vfs.Lock()
			defer vfs.Unlock()
			h, err := dw.GetToken(cookie)
			if err != nil {
				return errorToRIO(err)
			}
			msg.Handle[0] = h
			msg.Hcount = 1
			return zx.ErrOk
		case fdio.IoctlVFSUnmountFS:
			// Shut down filesystem
			err := vfs.fs.Close()
			if err != nil {
				fmt.Println("Error closing Filesystem: %#v", err)
			}
			// Close reply handle, indicating that the unmounting process is complete
			rh.Close()
			os.Exit(0)
		case fdio.IoctlVFSQueryFS:
			maxlen := uint32(msg.Arg)
			totalBytes := uint64(vfs.fs.Size())
			usedBytes := totalBytes - uint64(vfs.fs.FreeSize())

			queryInfo := VfsQueryInfo{
				TotalBytes: totalBytes,
				UsedBytes:  usedBytes,
				TotalNodes: 0,
				UsedNodes:  0,
			}

			name := append([]byte(vfs.fs.Type()), 0)

			const infoSize = uint32(unsafe.Sizeof(queryInfo))
			totalSize := infoSize + uint32(len(name))

			if totalSize > maxlen {
				return zx.ErrBufferTooSmall
			}

			copy(msg.Data[0:], (*[infoSize]byte)(unsafe.Pointer(&queryInfo))[:])
			copy(msg.Data[infoSize:], name)
			msg.Datalen = totalSize
			return zx.Status(msg.Datalen)
		case fdio.IoctlVFSGetDevicePath:
			path := vfs.fs.DevicePath()
			copy(msg.Data[0:], path)
			msg.Datalen = uint32(len(path))
			return zx.Status(msg.Datalen)
		default:
			return zx.ErrNotSupported
		}
	case fdio.OpSetAttr:
		atime, mtime := getTimeShared(msg)
		return errorToRIO(dir.Touch(atime, mtime))
	default:
		println("ThinFS DIR UNKNOWN OP: ", msg.Op())
		msg.DiscardHandles()
		return zx.ErrNotSupported
	}
	return zx.ErrNotSupported
}

func (vfs *ThinVFS) fdioServer(msg *fdio.Msg, rh zx.Handle, cookie int64) zx.Status {
	if msg.Hcount != msg.OpHandleCount() {
		// Incoming number of handles must match message type
		msg.DiscardHandles()
		return zx.ErrIO
	}

	// Determine if the object we're acting on is a directory or a file
	vfs.Lock()
	obj := vfs.files[int64(cookie)]
	vfs.Unlock()
	if obj == nil && msg.Op() == fdio.OpClose {
		// Removing object that has already been removed
		return zx.ErrOk
	}
	switch obj := obj.(type) {
	case fs.File:
		return vfs.processOpFile(msg, obj, cookie)
	case *directoryWrapper:
		return vfs.processOpDirectory(msg, rh, obj, cookie)
	default:
		fmt.Printf("cookie %d resulted in unexpected type %T\n", cookie, obj)
		msg.DiscardHandles()
		return zx.ErrInternal
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
