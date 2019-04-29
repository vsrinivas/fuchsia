// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package memory

import (
	"golang.org/x/sys/unix"
)

func total() uint64 {
	memsize, err := unix.SysctlUint64("hw.memsize")
	if err != nil {
		return 0
	}
	return memsize
}
