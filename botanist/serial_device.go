// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"io"

	"fuchsia.googlesource.com/tools/serial"
)

// SerialDevice is the interface to interacting with a serial device.
type SerialDevice struct {
	ctx context.Context
	io.ReadWriteCloser
}

// NewSerialDevice returns a new SerialDevice which is usable as io.ReadWriteCloser.
func NewSerialDevice(ctx context.Context, device string) (*SerialDevice, error) {
	s, err := serial.Open(device)
	if err != nil {
		return nil, err
	}
	return &SerialDevice{
		ctx:             ctx,
		ReadWriteCloser: s,
	}, nil
}

// Read is a thin wrapper around io.Read() which respects Context.cancel() so that we may cancel an in-flight io.Copy().
func (s *SerialDevice) Read(p []byte) (int, error) {
	select {
	case <-s.ctx.Done():
		return 0, s.ctx.Err()
	default:
		return s.ReadWriteCloser.Read(p)
	}
}
