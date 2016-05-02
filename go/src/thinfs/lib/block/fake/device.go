// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package fake provides a fake in-memory implementation of block.Device.
package fake

import (
	"github.com/pkg/errors"
)

const blockSize = 1024

var (
	// ErrBlockSize indicates that one of the provided arguments is not a
	// multiple of BlockSize().
	ErrBlockSize = errors.New("argument is not a multiple of blocksize")

	// ErrOutOfBounds indicates that the requested range for a ReadAt or
	// WriteAt call is out of bounds.
	ErrOutOfBounds = errors.New("range is out of bounds")
)

// Device implements block.Device using a []byte.
type Device []byte

func (d Device) check(p []byte, off int64) error {
	if off%blockSize != 0 {
		return errors.Wrap(ErrBlockSize, "off")
	}

	if int64(len(p))%blockSize != 0 {
		return errors.Wrap(ErrBlockSize, "len(p)")
	}

	if off+int64(len(p)) > d.Size() {
		return errors.Wrapf(ErrOutOfBounds, "[%v, %v)", off, off+int64(len(p)))
	}

	return nil
}

// BlockSize implements block.Device.BlockSize for Device.
func (Device) BlockSize() int64 {
	return blockSize
}

// Size implements block.Device.Size for Device.
func (d Device) Size() int64 {
	return int64(len(d))
}

// ReadAt implements block.Device.ReadAt for Device.
func (d Device) ReadAt(p []byte, off int64) (int, error) {
	if err := d.check(p, off); err != nil {
		return 0, err
	}

	return copy(p, d[off:]), nil
}

// WriteAt implements block.Device.WriteAt for Device.
func (d Device) WriteAt(p []byte, off int64) (int, error) {
	if err := d.check(p, off); err != nil {
		return 0, err
	}

	return copy(d[off:], p), nil
}

// Flush implements block.Device.Flush for Device.
func (Device) Flush() error {
	return nil
}

// Discard implements block.Device.Discard for Device.
func (Device) Discard(off, len int64) error {
	return nil
}

// Close implements block.Device.Close for Device.
func (Device) Close() error {
	return nil
}
