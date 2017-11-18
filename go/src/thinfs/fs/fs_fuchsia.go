// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package fs

import "syscall/zx"

// Remote can be returned by Open in order to hand off the open transaction to another filesystem.
type Remote struct {
	// Channel is the remote directory channel the request is to be forwarded to.
	Channel *zx.Channel
	// Path is the new path to be opened at the remote.
	Path string
	// Flags are the open flags to be sent to the remote.
	Flags OpenFlags
}
