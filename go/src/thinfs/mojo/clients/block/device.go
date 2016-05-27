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

// Package block implements the thinfs/lib/block.Device interface using mojo.
package block

import (
	"fmt"
	"interfaces/block"
	mojoerr "interfaces/errors"
	"mojo/public/go/bindings"

	"github.com/pkg/errors"
)

// Errors that may be returned by functions in this package.
var (
	// Errors specific to this package.
	ErrClosed    = errors.New("device is closed")
	ErrUnaligned = errors.New("parameter is not blocksize aligned")

	// Errors that come from the block device mojo service.
	ErrCancelled       = errors.New("operation was canceled")
	ErrUnknown         = errors.New("unknown error")
	ErrInvalid         = errors.New("invalid argument")
	ErrDeadline        = errors.New("deadline exceeded")
	ErrNotFound        = errors.New("requested entity was not found")
	ErrExists          = errors.New("requested entity already exists")
	ErrDenied          = errors.New("permission denied")
	ErrUnauthenticated = errors.New("request does not have valid authentication credentials")
	ErrExhausted       = errors.New("resource exhausted")
	ErrPrecondition    = errors.New("failed precondition")
	ErrAborted         = errors.New("operation was aborted")
	ErrOutOfRange      = errors.New("out of range access")
	ErrUnimplemented   = errors.New("operation not implemented")
	ErrUnavailable     = errors.New("device is currently unavailable")
	ErrDataLoss        = errors.New("unrecoverable data loss or corruption")
	ErrInternal        = errors.New("internal error")
)

func convertError(err mojoerr.Error) error {
	switch err {
	case mojoerr.Error_Ok:
		return nil
	case mojoerr.Error_Cancelled:
		return ErrCancelled
	case mojoerr.Error_Unknown:
		return ErrUnknown
	case mojoerr.Error_InvalidArgument:
		return ErrInvalid
	case mojoerr.Error_DeadlineExceeded:
		return ErrDeadline
	case mojoerr.Error_NotFound:
		return ErrNotFound
	case mojoerr.Error_AlreadyExists:
		return ErrExists
	case mojoerr.Error_PermissionDenied:
		return ErrDenied
	case mojoerr.Error_Unauthenticated:
		return ErrUnauthenticated
	case mojoerr.Error_ResourceExhausted:
		return ErrExhausted
	case mojoerr.Error_FailedPrecondition:
		return ErrPrecondition
	case mojoerr.Error_Aborted:
		return ErrAborted
	case mojoerr.Error_OutOfRange:
		return ErrOutOfRange
	case mojoerr.Error_Unimplemented:
		return ErrUnimplemented
	case mojoerr.Error_Unavailable:
		return ErrUnavailable
	case mojoerr.Error_DataLoss:
		return ErrDataLoss
	case mojoerr.Error_Internal:
		return ErrInternal
	default:
		panic(fmt.Sprint("unknown error code: ", err))
	}
}

// Device implements thinfs/lib/block.Device over mojo ipc.
type Device struct {
	proxy     *block.Device_Proxy
	blocksize int64
	size      int64
	caps      block.Capabilities
	err       error
}

// New creates a new Device from a mojo handle pointing to a block.Device.
func New(ptr block.Device_Pointer) (*Device, error) {
	proxy := block.NewDeviceProxy(ptr, bindings.GetAsyncWaiter())
	blocksize, outErr, err := proxy.BlockSize()
	if err != nil {
		return nil, errors.Wrap(err, "proxy error while calling BlockSize")
	}

	if err := convertError(outErr); err != nil {
		return nil, errors.Wrap(err, "unable to fetch block size")
	}

	size, outErr, err := proxy.Size()
	if err != nil {
		return nil, errors.Wrap(err, "proxy error while calling Size()")
	}

	if err := convertError(outErr); err != nil {
		return nil, errors.Wrap(err, "unable to fetch device size")
	}

	caps, outErr, err := proxy.GetCapabilities()
	if err != nil {
		return nil, errors.Wrap(err, "proxy error while calling GetCapabilities")
	}

	if err := convertError(outErr); err != nil {
		return nil, errors.Wrap(err, "unable to fetch device capabilities")
	}

	return &Device{
		proxy:     proxy,
		blocksize: blocksize,
		size:      size,
		caps:      caps,
	}, nil
}

// GetCapabilities returns the capabilities associated with the client's handle
// to the block device.
func (d *Device) GetCapabilities() block.Capabilities {
	return d.caps
}

// BlockSize implements thinfs/lib/block.Device.BlockSize for Device.
func (d *Device) BlockSize() int64 {
	return d.blocksize
}

// Size implements thinfs/lib/block.Device.Size for Device.
func (d *Device) Size() int64 {
	return d.size
}

// check checks the parameters to make sure they are blocksize aligned.
func (d *Device) check(off, len int64) error {
	if off%d.blocksize != 0 {
		return errors.Wrap(ErrUnaligned, "offset")
	}

	if len%d.blocksize != 0 {
		return errors.Wrap(ErrUnaligned, "length")
	}

	return nil
}

// ReadAt implements thinfs/lib/block.Device.ReadAt for Device.
func (d *Device) ReadAt(p []byte, off int64) (int, error) {
	if d.err != nil {
		return 0, d.err
	}

	if err := d.check(off, int64(len(p))); err != nil {
		return 0, errors.Wrap(err, "ReadAt")
	}

	block := off / d.blocksize
	count := int64(len(p)) / d.blocksize

	data, outErr, err := d.proxy.ReadBlocks(block, count)
	n := copy(p, data)

	if err != nil {
		d.err = err
		return n, errors.Wrap(err, "ReadAt proxy error")
	}

	if err := convertError(outErr); err != nil {
		return n, errors.Wrapf(err, "unable to read %v bytes from offset %v", len(p), off)
	}

	return n, nil
}

// WriteAt implements thinfs/lib/block.Device.WriteAt for Device.
func (d *Device) WriteAt(p []byte, off int64) (int, error) {
	if d.err != nil {
		return 0, d.err
	}

	if err := d.check(off, int64(len(p))); err != nil {
		return 0, errors.Wrap(err, "WriteAt")
	}

	block := off / d.blocksize
	count := int64(len(p)) / d.blocksize

	w, outErr, err := d.proxy.WriteBlocks(block, count, p)
	n := int(w)

	if err != nil {
		d.err = err
		return n, errors.Wrap(err, "WriteAt proxy error")
	}

	if err := convertError(outErr); err != nil {
		return n, errors.Wrapf(err, "unable to write %v bytes to offset %v", len(p), off)
	}

	return n, nil
}

// Flush implements thinfs/lib/block.Device.Flush for Device.
func (d *Device) Flush() error {
	if d.err != nil {
		return d.err
	}

	outErr, err := d.proxy.Flush()

	if err != nil {
		d.err = err
		return errors.Wrap(err, "Flush proxy error")
	}

	if err := convertError(outErr); err != nil {
		return errors.Wrap(err, "unable to flush device")
	}

	return nil
}

// Discard implements thinfs/lib/block.Device.Discard for Device.
func (d *Device) Discard(off, len int64) error {
	if d.err != nil {
		return d.err
	}

	if err := d.check(off, len); err != nil {
		return errors.Wrap(err, "Discard")
	}

	block := off / d.blocksize
	count := len / d.blocksize

	outErr, err := d.proxy.DiscardBlocks(block, count)

	if err != nil {
		d.err = err
		return err
	}

	if err := convertError(outErr); err != nil {
		return errors.Wrapf(err, "unable to discard %v bytes from offset %v", len, off)
	}

	return nil
}

// Close implements thinfs/lib/block.Device.Close for Device.
func (d *Device) Close() error {
	if err := d.Flush(); err != nil {
		return errors.Wrap(err, "Close")
	}

	d.proxy.Close_Proxy()
	d.err = ErrClosed
	d.proxy = nil

	return nil
}
