// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia
// +build !build_with_native_toolchain

package fs

import (
	"syscall/zx"

	"fidl/fuchsia/mem"
)

// Remote can be returned by Open in order to hand off the open transaction to another filesystem.
type Remote struct {
	// Channel is the remote directory channel the request is to be forwarded to.
	Channel zx.Channel
	// Path is the new path to be opened at the remote.
	Path string
	// Flags are the open flags to be sent to the remote.
	Flags OpenFlags
}

type FileWithGetBuffer interface {
	// Acquires a buffer representing this file, if there is one, with the
	// requested access rights.
	//
	// flags must be one of VmoFlag*
	GetBuffer(flags uint32) (*mem.Buffer, error)
}
