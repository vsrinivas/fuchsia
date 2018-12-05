// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serial

import (
	"io"
)

// Open opens a new serial port with the given name and baud rate.
func Open(name string, baudRate int, timeoutSecs int) (io.ReadWriteCloser, error) {
	return open(name, baudRate, timeoutSecs)
}
