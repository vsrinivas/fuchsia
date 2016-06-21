// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// +build fuchsia

package ext2fs

// #include "manager_fuchsia.h"
// #include <stdlib.h>
// #include <ext2fs.h>
// #include <ext2_io.h>
import "C"

import (
	"os"
	"strconv"
	"syscall"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/block"
	"fuchsia.googlesource.com/thinfs/lib/cpointer"
	"fuchsia.googlesource.com/thinfs/lib/thinio"
	"github.com/golang/glog"
)

// ioManager represents the I/O manager that libext2fs uses on fuchsia devices.
var ioManager = C.fuchsia_io_manager

const (
	defaultBlockSize = 1024
	defaultCacheSize = 8 * 1024 * 1024
)

type channelData struct {
	flags     C.int
	blocksize int
	offset    int64
	conductor *thinio.Conductor
	stats     C.struct_struct_io_stats // *facepalm*
}

func convertError(err error) C.errcode_t {
	for {
		switch e := err.(type) {
		case *os.PathError:
			err = e.Err
		case syscall.Errno:
			return C.errcode_t(e)
		default:
			return -1
		}
	}
}

func validate(channel C.io_channel) (*channelData, C.errcode_t) {
	if channel.magic != C.EXT2_ET_MAGIC_IO_CHANNEL {
		return nil, C.EXT2_ET_MAGIC_IO_CHANNEL
	}

	val, err := cpointer.Value(uintptr(channel.private_data))
	if err != nil {
		return nil, C.EXT2_ET_BAD_MAGIC
	}

	return val.(*channelData), 0
}

//export fuchsiaOpen
func fuchsiaOpen(name *C.char, flags C.int, channel *C.io_channel) C.errcode_t {
	if name == nil {
		return C.EXT2_ET_BAD_DEVICE_NAME
	}

	path := C.GoString(name)
	if glog.V(1) {
		glog.Infof("Opening file system at %s\n", path)
	}

	var io C.io_channel
	if err := C.ext2fs_get_mem(C.sizeof_struct_struct_io_channel, unsafe.Pointer(&io)); err != 0 {
		return err
	}
	C.memset(unsafe.Pointer(io), 0, C.sizeof_struct_struct_io_channel)

	io.magic = C.EXT2_ET_MAGIC_IO_CHANNEL
	io.manager = C.fuchsia_io_manager

	if err := C.ext2fs_get_mem(C.ulong(C.strlen(name)+1), unsafe.Pointer(&io.name)); err != 0 {
		C.ext2fs_free_mem(unsafe.Pointer(&io))
		return err
	}
	C.strcpy(io.name, name)

	priv := &channelData{
		flags:     flags,
		blocksize: defaultBlockSize,
	}
	priv.stats.num_fields = 2 // Not sure why this is necessary but the unix_io_manager does it.

	io.private_data = unsafe.Pointer(cpointer.New(priv))
	io.block_size = defaultBlockSize
	io.read_error = nil
	io.write_error = nil
	io.refcount = 1

	// handleError is used to make sure everything is cleaned up properly if an error occurs.
	handleError := func(err error) C.errcode_t {
		cpointer.MustDelete(uintptr(io.private_data))
		C.ext2fs_free_mem(unsafe.Pointer(&io.name))
		C.ext2fs_free_mem(unsafe.Pointer(&io))

		return convertError(err)
	}

	h, err := strconv.ParseInt(path, 0, 64)
	if err != nil {
		handleError(err)
		return C.EXT2_ET_BAD_DEVICE_NAME
	}
	dev, err := cpointer.Value(uintptr(h))
	if err != nil {
		handleError(err)
		return C.EXT2_ET_BAD_DEVICE_NAME
	}

	priv.conductor = thinio.NewConductor(dev.(block.Device), defaultCacheSize)
	*channel = io
	return 0
}

//export fuchsiaClose
func fuchsiaClose(channel C.io_channel) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	if channel.refcount--; channel.refcount > 0 {
		return 0
	}

	if glog.V(1) {
		glog.Infof("Closing file system at %s\n", C.GoString(channel.name))
	}

	cleanup := func() {
		cpointer.MustDelete(uintptr(channel.private_data))
		channel.private_data = nil

		C.ext2fs_free_mem(unsafe.Pointer(&channel.name))
		C.ext2fs_free_mem(unsafe.Pointer(&channel))
	}

	if err := priv.conductor.Close(); err != nil {
		cleanup()
		return convertError(err)
	}

	cleanup()
	return 0
}

//export fuchsiaSetBlockSize
func fuchsiaSetBlockSize(channel C.io_channel, blocksize int) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	if glog.V(2) {
		glog.Infof("Setting blocksize to %v\n", blocksize)
	}

	if priv.blocksize != blocksize {
		channel.block_size = C.int(blocksize)
		priv.blocksize = blocksize
	}

	return 0
}

//export fuchsiaReadBlock
func fuchsiaReadBlock(channel C.io_channel, block C.ulong, count C.int, data unsafe.Pointer) C.errcode_t {
	return fuchsiaReadBlock64(channel, C.ulonglong(block), count, data)
}

//export fuchsiaWriteBlock
func fuchsiaWriteBlock(channel C.io_channel, block C.ulong, count C.int, data unsafe.Pointer) C.errcode_t {
	return fuchsiaWriteBlock64(channel, C.ulonglong(block), count, data)
}

//export fuchsiaFlush
func fuchsiaFlush(channel C.io_channel) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	if glog.V(2) {
		glog.Infof("Flushing data for file system at %s\n", C.GoString(channel.name))
	}

	if err := priv.conductor.Flush(); err != nil {
		return convertError(err)
	}

	return 0
}

//export fuchsiaWriteByte
func fuchsiaWriteByte(channel C.io_channel, offset C.ulong, count C.int, data unsafe.Pointer) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	var size int
	if count < 0 {
		size = int(-count)
	} else {
		size = int(count)
	}

	off := priv.offset + int64(offset)

	if glog.V(2) {
		glog.Infof("Writing %v bytes to offset #%v from address %#x\n", size, off, data)
	}

	p := (*[1 << 30]byte)(data)[:size:size]
	n, err := priv.conductor.WriteAt(p, off)
	if n < len(p) {
		return C.EXT2_ET_SHORT_WRITE
	} else if err != nil {
		return convertError(err)
	}

	priv.stats.bytes_written += C.ulonglong(len(p))
	return 0
}

//export fuchsiaSetOption
func fuchsiaSetOption(channel C.io_channel, option, arg *C.char) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	if C.GoString(option) == "offset" {
		if arg == nil {
			return C.EXT2_ET_INVALID_ARGUMENT
		}

		off, err := strconv.ParseInt(C.GoString(arg), 0, 64)
		if err != nil {
			return C.EXT2_ET_INVALID_ARGUMENT
		}
		if off < 0 {
			return C.EXT2_ET_INVALID_ARGUMENT
		}

		priv.offset = off
		return 0
	}

	return C.EXT2_ET_INVALID_ARGUMENT
}

//export fuchsiaGetStats
func fuchsiaGetStats(channel C.io_channel, stats *C.io_stats) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	if stats == nil {
		return C.EXT2_ET_INVALID_ARGUMENT
	}

	*stats = &priv.stats
	return 0
}

//export fuchsiaReadBlock64
func fuchsiaReadBlock64(channel C.io_channel, block C.ulonglong, count C.int, data unsafe.Pointer) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	var size int
	if count < 0 {
		size = int(-count)
	} else {
		size = int(count) * priv.blocksize
	}

	if glog.V(2) {
		glog.Infof("Reading %v bytes from block #%v to address %#x\n", size, block, data)
	}

	p := (*[1 << 30]byte)(data)[:size:size]
	n, err := priv.conductor.ReadAt(p, int64(block)*int64(priv.blocksize))
	if n < len(p) {
		return C.EXT2_ET_SHORT_READ
	} else if err != nil {
		return convertError(err)
	}

	priv.stats.bytes_read += C.ulonglong(n)
	return 0
}

//export fuchsiaWriteBlock64
func fuchsiaWriteBlock64(channel C.io_channel, block C.ulonglong, count C.int, data unsafe.Pointer) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	var size int
	if count < 0 {
		size = int(-count)
	} else {
		size = int(count) * priv.blocksize
	}

	if glog.V(2) {
		glog.Infof("Writing %v bytes to block #%v from address %#x\n", size, block, data)
	}

	p := (*[1 << 30]byte)(data)[:size:size]
	n, err := priv.conductor.WriteAt(p, int64(block)*int64(priv.blocksize))
	if n < len(p) {
		return C.EXT2_ET_SHORT_WRITE
	} else if err != nil {
		return convertError(err)
	}

	priv.stats.bytes_written += C.ulonglong(n)
	return 0
}

//export fuchsiaDiscard
func fuchsiaDiscard(channel C.io_channel, block, count C.ulonglong) C.errcode_t {
	priv, errcode := validate(channel)
	if errcode != 0 {
		return errcode
	}

	off, len := int64(block)*int64(priv.blocksize), int64(count)*int64(priv.blocksize)
	if glog.V(2) {
		glog.Infof("Discarding data in range [%v, %v)", off, off+len)
	}
	if err := priv.conductor.Discard(off, len); err != nil {
		return convertError(err)
	}

	return 0
}
