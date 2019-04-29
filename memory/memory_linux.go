// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package memory

import (
	"syscall"
)

func total() uint64 {
	info := &syscall.Sysinfo_t{}
	err := syscall.Sysinfo(info)
	if err != nil {
		return 0
	}
	return uint64(info.Totalram) * uint64(info.Unit)
}
