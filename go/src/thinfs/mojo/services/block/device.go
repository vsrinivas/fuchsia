// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package block implements the block.Device mojo interface using a
// thinfs/lib/block.Device.
package block

import (
	"fmt"
	"interfaces/block"
	mojoerr "interfaces/errors"

	blk "fuchsia.googlesource.com/thinfs/lib/block"
	"github.com/pkg/errors"
)

// ErrClosed indicates that the device has been closed.
var ErrClosed = errors.New("device is closed")

// Device implements block.Device using a thinfs/lib/block.Device.
type Device struct {
	dev  blk.Device
	caps block.Capabilities
}

// New creates and returns a new, initialized Device.
func New(dev blk.Device, caps block.Capabilities) *Device {
	return &Device{dev, caps}
}

// GetCapabilities implements block.Device.GetCapabilities for Device.
func (d *Device) GetCapabilities() (block.Capabilities, mojoerr.Error, error) {
	return d.caps, mojoerr.Error_Ok, nil
}

// SetCapabilities implements block.Device.SetCapabilities for Device.
func (d *Device) SetCapabilities(caps block.Capabilities) (mojoerr.Error, error) {
	err := mojoerr.Error_Ok
	switch caps {
	case block.Capabilities_None:
		// Dropping all permissions is always allowed.
		d.caps = block.Capabilities_None
	case block.Capabilities_ReadOnly:
		if d.caps == block.Capabilities_ReadOnly || d.caps == block.Capabilities_ReadWrite {
			d.caps = block.Capabilities_ReadOnly
		} else {
			err = mojoerr.Error_PermissionDenied
		}
	case block.Capabilities_ReadWrite:
		// It's not actually possible to gain read/write permission this way.  Either the
		// client already has it or it doesn't.
		if d.caps != block.Capabilities_ReadWrite {
			err = mojoerr.Error_PermissionDenied
		}
	default:
		err = mojoerr.Error_InvalidArgument
	}

	return err, nil
}

// BlockSize implements block.Device.BlockSize for Device.
func (d *Device) BlockSize() (int64, mojoerr.Error, error) {
	if d.dev == nil {
		return -1, mojoerr.Error_FailedPrecondition, ErrClosed
	}

	return d.dev.BlockSize(), mojoerr.Error_Ok, nil
}

// Size implements block.Device.Size for Device.
func (d *Device) Size() (int64, mojoerr.Error, error) {
	if d.dev == nil {
		return -1, mojoerr.Error_FailedPrecondition, ErrClosed
	}

	return d.dev.Size(), mojoerr.Error_Ok, nil
}

// ReadBlocks implements block.Device.ReadBlocks for Device.
func (d *Device) ReadBlocks(pos, count int64) ([]uint8, mojoerr.Error, error) {
	if d.dev == nil {
		return nil, mojoerr.Error_FailedPrecondition, ErrClosed
	}

	if d.caps == block.Capabilities_None {
		return nil, mojoerr.Error_PermissionDenied, nil
	}

	blocksize := d.dev.BlockSize()
	off, size := pos*blocksize, count*blocksize
	if off+size > d.dev.Size() {
		return nil, mojoerr.Error_OutOfRange, nil
	}

	p := make([]uint8, size)
	if _, err := d.dev.ReadAt(p, off); err != nil {
		return nil, mojoerr.Error_Internal, errors.Wrap(err, fmt.Sprintf("unable to read blocks %v to %v", pos, pos+count))
	}

	return p, mojoerr.Error_Ok, nil
}

// WriteBlocks implements block.Device.WriteBlocks for Device.
func (d *Device) WriteBlocks(pos, count int64, p []uint8) (int64, mojoerr.Error, error) {
	if d.dev == nil {
		return 0, mojoerr.Error_FailedPrecondition, ErrClosed
	}

	if d.caps != block.Capabilities_ReadWrite {
		return 0, mojoerr.Error_PermissionDenied, nil
	}

	blocksize := d.dev.BlockSize()
	off, size := pos*blocksize, count*blocksize
	if off+size > d.dev.Size() {
		return 0, mojoerr.Error_OutOfRange, nil
	}
	if int64(len(p)) != size {
		return 0, mojoerr.Error_InvalidArgument, nil
	}

	n, err := d.dev.WriteAt(p, off)
	if err != nil {
		return int64(n), mojoerr.Error_Internal, errors.Wrap(err, fmt.Sprintf("unable to write blocks %v to %v", pos, pos+count))
	}

	return int64(n), mojoerr.Error_Ok, nil
}

// DiscardBlocks implements block.Device.DiscardBlocks for Device.
func (d *Device) DiscardBlocks(pos, count int64) (mojoerr.Error, error) {
	if d.dev == nil {
		return mojoerr.Error_FailedPrecondition, ErrClosed
	}

	if d.caps != block.Capabilities_ReadWrite {
		return mojoerr.Error_PermissionDenied, nil
	}

	blocksize := d.dev.BlockSize()
	off, size := pos*blocksize, count*blocksize
	if off+size > d.dev.Size() {
		return mojoerr.Error_OutOfRange, nil
	}

	if err := d.dev.Discard(off, size); err != nil {
		return mojoerr.Error_Internal, errors.Wrap(err, fmt.Sprintf("unable to discard blocks %v to %v", pos, pos+count))
	}

	return mojoerr.Error_Ok, nil
}

// Barrier implements block.Device.Barrier for Device.
func (d *Device) Barrier() (mojoerr.Error, error) {
	return d.Flush()
}

// Flush implements block.Device.Flush for Device.
func (d *Device) Flush() (mojoerr.Error, error) {
	if d.dev == nil {
		return mojoerr.Error_FailedPrecondition, ErrClosed
	}

	if d.caps != block.Capabilities_ReadWrite {
		return mojoerr.Error_PermissionDenied, nil
	}

	if err := d.dev.Flush(); err != nil {
		return mojoerr.Error_Internal, errors.Wrap(err, "unable to flush device")
	}

	return mojoerr.Error_Ok, nil
}

// Close closes the Device, rendering it unusable for I/O.  Any future operations
// on the Device will return Error_FailedPrecondition.  The returned error is strictly
// informational and the Close operation may not be retried.
func (d *Device) Close() error {
	if d.dev == nil {
		return errors.Wrap(ErrClosed, "attempting to close device twice")
	}

	defer func() {
		d.dev = nil
		d.caps = block.Capabilities_None
	}()

	if err := d.dev.Close(); err != nil {
		return errors.Wrap(err, "unable to close device")
	}

	return nil
}
