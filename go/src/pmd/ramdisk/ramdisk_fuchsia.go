// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package ramdisk

// #cgo CFLAGS: -I${SRCDIR}/../../../../../zircon/system/ulib/fs-management/include
// #cgo LDFLAGS: -lfs-management
// #include <fs-management/ramdisk.h>
// #include <fs-management/mount.h>
// #include <fcntl.h>
//
// int blobfs_mount(char *dpath, char *path) {
//   int fd = open(dpath, O_RDWR);
//   return mount(fd, path, DISK_FORMAT_BLOBFS, &default_mount_options, &launch_stdio_async);
// }
// int blobfs_mkfs(char *path) {
//   return mkfs(path, DISK_FORMAT_BLOBFS, &launch_stdio_sync, &default_mkfs_options);
// }
import "C"

import (
	"bytes"
	"fmt"
	"runtime"
	"syscall"
	"unsafe"
)

type Ramdisk struct {
	path [syscall.PathMax]byte
}

// New constructs and creates a ramdisk of size bytes at the given path.
func New(size int) (*Ramdisk, error) {
	r := &Ramdisk{}
	return r, r.Create(512, uint64(size)/512)
}

func (r *Ramdisk) Create(blkSz uint64, blkCnt uint64) error {
	n := C.create_ramdisk(C.uint64_t(blkSz), C.uint64_t(blkCnt), (*C.char)(unsafe.Pointer(&r.path[0])))
	if n == 0 {
		runtime.SetFinalizer(r, finalizeRamdisk)
		return nil
	}
	return fmt.Errorf("ramdisk creation failed with %d", n)
}

func (r *Ramdisk) Destroy() error {
	n := C.destroy_ramdisk((*C.char)(unsafe.Pointer(&r.path[0])))
	if n == 0 {
		return nil
	}
	return fmt.Errorf("ramdisk destruction failed with %d", n)
}

func (r *Ramdisk) Path() string {
	return string(r.path[:bytes.Index(r.path[:], []byte{0})])
}

func (r *Ramdisk) MkfsBlobfs() error {
	n := C.blobfs_mkfs(C.CString(r.Path()))
	if n == 0 {
		return nil
	}
	return fmt.Errorf("mkfs failed with %d", n)
}
func (r *Ramdisk) MountBlobfs(mountPath string) error {
	n := C.blobfs_mount(C.CString(r.Path()), C.CString(mountPath))
	if n == 0 {
		return nil
	}
	return fmt.Errorf("mount failed with %d", n)
}

func (r *Ramdisk) Umount(mountPath string) error {
	n := C.umount(C.CString(mountPath))
	if n == 0 {
		return nil
	}
	return fmt.Errorf("umount failed with %d", n)
}

func finalizeRamdisk(r *Ramdisk) {
	r.Destroy()
}
