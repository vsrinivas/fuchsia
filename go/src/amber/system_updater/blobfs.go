// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"fmt"

	"syscall"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/io"
)

// Blobfs exposes the admin interface of /blob.
type Blobfs struct {
	io.DirectoryAdminInterface
}

// OpenBlobfs opens a handle to the directory admin interface at /blob.
func OpenBlobfs() (*Blobfs, error) {
	rootFDIO, err := syscall.OpenPath("/blob", 0, 0644)
	if err != nil {
		return nil, fmt.Errorf("can't open /blob: %s", err)
	}
	defer rootFDIO.Close()
	rootIO, err := rootFDIO.Clone()
	if err != nil {
		return nil, fmt.Errorf("can't clone blobfs root handle: %s", err)
	}
	handles := rootIO.Handles()
	for _, h := range handles[1:] {
		h.Close()
	}
	channel := zx.Channel(handles[0])

	blobfs := io.DirectoryAdminInterface(fidl.ChannelProxy{Channel: channel})

	return &Blobfs{DirectoryAdminInterface: blobfs}, nil
}

// QueryFreeSpace returns the number of unused bytes allocated to blobfs.
func (b *Blobfs) QueryFreeSpace() (int64, error) {
	status, info, err := b.QueryFilesystem()
	if err != nil {
		return 0, err
	}
	if zx.Status(status) != zx.ErrOk {
		return 0, fmt.Errorf("%d", status)
	}

	freeAllocatedBytes := info.TotalBytes - info.UsedBytes

	return int64(info.FreeSharedPoolBytes + freeAllocatedBytes), nil
}
