// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package ramdisk

// #cgo LDFLAGS: -lramdevice-client -lfdio -lzircon
// #include <ramdevice-client/ramdisk.h>
// #include <lib/fdio/directory.h>
// #include <lib/fdio/fd.h>
// #include <lib/fdio/fdio.h>
// #include <lib/fdio/spawn.h>
// #include <lib/fdio/vfs.h>
// #include <string.h>
// #include <zircon/device/vfs.h>
// #include <zircon/syscalls.h>
//
// zx_status_t ramdisk_blobfs_mkfs(const ramdisk_client_t* client, zx_handle_t* process_out) {
// 	zx_status_t status = ZX_OK;
// 	fdio_spawn_action_t actions[1] = {
// 		{
// 			.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
// 			.h = {
// 				.id = FS_HANDLE_BLOCK_DEVICE_ID,
// 				.handle = ZX_HANDLE_INVALID,
// 			}
// 		},
// 	};
//
// 	status = fdio_fd_clone(ramdisk_get_block_fd(client), &actions[0].h.handle);
// 	if (status != ZX_OK) {
// 		fprintf(stderr, "failed to get service handle! %d\n", status);
// 		return status;
// 	}
// 	if (actions[0].h.handle == ZX_HANDLE_INVALID) {
// 		fprintf(stderr, "handle invalid after clone\n");
// 		return ZX_ERR_INTERNAL;
// 	}
// 	char* argv[5] = {"/pkg/bin/blobfs", "-m", "mkfs", 0};
// 	return fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], (const char* const *)argv, NULL, 1, actions, process_out, NULL);
// }
//
// zx_status_t ramdisk_blobfs_mount(const ramdisk_client_t* client, zx_handle_t dir_request, zx_handle_t* process_out) {
// 	zx_status_t status = ZX_OK;
// 	zx_handle_t export_root_client, export_root_server;
// 	status = zx_channel_create(0, &export_root_client, &export_root_server);
// 	if (status != ZX_OK) {
// 		fprintf(stderr, "unable to create channel\n");
// 		return status;
// 	}
// 	fdio_spawn_action_t actions[2] = {
// 		{
// 			.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
// 			.h = {
// 				.id = FS_HANDLE_BLOCK_DEVICE_ID,
// 				.handle = ZX_HANDLE_INVALID,
// 			}
// 		},
// 		{
// 			.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
// 			.h = {
// 				.id = PA_DIRECTORY_REQUEST,
// 				.handle = export_root_server,
// 			}
// 		}
// 	};
//
// 	status = fdio_fd_clone(ramdisk_get_block_fd(client), &actions[0].h.handle);
// 	if (status != ZX_OK) {
// 		fprintf(stderr, "failed to get service handle! %d\n", status);
// 		zx_handle_close(export_root_client);
// 		zx_handle_close(export_root_server);
// 		return status;
// 	}
// 	if (actions[0].h.handle == ZX_HANDLE_INVALID) {
// 		fprintf(stderr, "handle invalid after clone\n");
// 		zx_handle_close(export_root_client);
// 		zx_handle_close(export_root_server);
// 		return ZX_ERR_INTERNAL;
// 	}
// 	char* argv[5] = {"/pkg/bin/blobfs", "-m", "mount", 0};
// 	status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], (const char* const *)argv, NULL, 2, actions, process_out, NULL);
// 	if (status != ZX_OK) {
// 		zx_handle_close(export_root_client);
// 		return status;
// 	}
// 	status = fdio_open_at(export_root_client, "root", ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE, dir_request);
// 	zx_handle_close(export_root_client);
// 	return status;
// }
import "C"

import (
	"context"
	"os"
	"runtime"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/io"
	"syscall/zx/zxwait"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pkgfs/iou"
)

type Ramdisk struct {
	ramdisk_client *C.struct_ramdisk_client
	proc           zx.Handle
	dir            *fdio.Directory
}

// New constructs and creates a ramdisk of size bytes at the given path.
func New(size int) (*Ramdisk, error) {
	r := &Ramdisk{}
	return r, r.create(512, uint64(size)/512)
}

func (r *Ramdisk) create(blkSz uint64, blkCnt uint64) error {
	n := C.ramdisk_create(C.uint64_t(blkSz), C.uint64_t(blkCnt), &r.ramdisk_client)
	if n == 0 {
		runtime.SetFinalizer(r, finalizeRamdisk)
		return nil
	}
	return &zx.Error{Status: zx.Status(n), Text: "ramdisk_create"}
}

func (r *Ramdisk) Destroy() error {
	if r.proc != zx.HandleInvalid {
		zx.Sys_task_kill(r.proc)
		r.dir.Close()
	}
	n := C.ramdisk_destroy(r.ramdisk_client)
	if n == 0 {
		return nil
	}
	return &zx.Error{Status: zx.Status(n), Text: "ramdisk_destroy"}
}

func (r *Ramdisk) StartBlobfs() error {
	status := C.ramdisk_blobfs_mkfs(r.ramdisk_client, (*C.uint)(&r.proc))
	if zx.Status(status) != zx.ErrOk {
		return &zx.Error{Status: zx.Status(status), Text: "ramdisk_blobfs_mkfs"}
	}
	if _, err := zxwait.WaitContext(context.Background(), r.proc, zx.SignalTaskTerminated); err != nil {
		return err
	}

	pxy, req, err := zx.NewChannel(0)
	if err != nil {
		return err
	}

	status = C.ramdisk_blobfs_mount(r.ramdisk_client, C.uint(uint32(zx.Handle(req))), (*C.uint)(&r.proc))
	if zx.Status(status) != zx.ErrOk {
		pxy.Close()
		return &zx.Error{Status: zx.Status(status), Text: "ramdisk_blobfs_mount"}
	}

	r.dir = fdio.NewDirectoryWithCtx(&io.DirectoryWithCtxInterface{Channel: pxy})
	return nil
}

func (r *Ramdisk) Open(path string, flags int, mode uint32) (*os.File, error) {
	return iou.OpenFrom(r.dir, path, flags, mode)
}

func finalizeRamdisk(r *Ramdisk) {
	r.Destroy()
}
