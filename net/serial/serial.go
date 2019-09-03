// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package serial provides support for serial connections.
package serial

import (
	"io"
)

const (
	defaultBaudRate    = 115200
	defaultTimeoutSecs = 10
)

// Open opens a new serial port using defaults.
func Open(name string) (io.ReadWriteCloser, error) {
	return OpenWithOptions(name, defaultBaudRate, defaultTimeoutSecs)
}

// OpenWithOptions opens a new serial port with the given name and baud rate.
func OpenWithOptions(name string, baudRate int, timeoutSecs int) (io.ReadWriteCloser, error) {
	return open(name, baudRate, timeoutSecs)
}
