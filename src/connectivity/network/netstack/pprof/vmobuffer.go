// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package pprof

import (
	"io"
	"os"
	"syscall/zx"

	"go.fuchsia.dev/fuchsia/src/lib/component"
)

var _ io.WriteCloser = (*vmoBuffer)(nil)

// vmoBuffer implements a self-resizing io.WriteCloser backed by a VMO.
type vmoBuffer struct {
	vmo    zx.VMO
	size   uint64
	offset uint64
}

func newVmoBuffer(size uint64) (*vmoBuffer, error) {
	vmo, err := zx.NewVMO(size, zx.VMOOptionResizable)
	if err != nil {
		return nil, err
	}
	return &vmoBuffer{vmo: vmo, size: size, offset: 0}, nil
}

func (b *vmoBuffer) grow() error {
	newSize := b.size * 2
	if err := b.vmo.SetSize(newSize); err != nil {
		return err
	}
	b.size = newSize
	return nil
}

func (b *vmoBuffer) Write(p []byte) (int, error) {
	if b.vmo == zx.VMO(zx.HandleInvalid) {
		return 0, os.ErrClosed
	}

	if len(p) > int(b.size-b.offset) {
		if err := b.grow(); err != nil {
			return 0, err
		}
	}

	if err := b.vmo.Write(p, b.offset); err != nil {
		return 0, err
	}
	b.offset += uint64(len(p))
	return len(p), nil
}

func (b *vmoBuffer) Close() error {
	vmo := b.vmo
	b.vmo = zx.VMO(zx.HandleInvalid)
	return vmo.Close()
}

// toVmoReader converts the vmoBuffer into a vmoReader that outputs the data
// that has been written into the vmoBuffer. The vmoBuffer should be considered
// to be consumed after calling toVmoReader(), and it is not valid to perform
// operations on it afterward.
func (b *vmoBuffer) toVmoReader() (*vmoReader, error) {
	if b.vmo == zx.VMO(zx.HandleInvalid) {
		return nil, os.ErrClosed
	}
	if err := b.vmo.SetSize(b.offset); err != nil {
		return nil, err
	}

	vmo := b.vmo
	b.vmo = zx.VMO(zx.HandleInvalid)

	return &vmoReader{
		vmo:    vmo,
		size:   b.offset,
		offset: 0,
	}, nil
}

// vmoReader implements a component.Reader backed by a VMO.
type vmoReader struct {
	vmo    zx.VMO
	size   uint64
	offset uint64
}

var _ component.Reader = (*vmoReader)(nil)

func (r *vmoReader) Read(p []byte) (int, error) {
	n, err := r.ReadAt(p, int64(r.offset))
	r.offset += uint64(n)
	return n, err
}

func (r *vmoReader) ReadAt(p []byte, off int64) (int, error) {
	if r.vmo == zx.VMO(zx.HandleInvalid) {
		return 0, os.ErrClosed
	}
	if off >= int64(r.size) {
		return 0, io.EOF
	}
	numBytesToRead := len(p)
	if bytesRemaining := int(r.size - uint64(off)); bytesRemaining < numBytesToRead {
		numBytesToRead = bytesRemaining
	}
	if err := r.vmo.Read(p[:numBytesToRead], uint64(off)); err != nil {
		return 0, err
	}
	return numBytesToRead, nil
}

func (r *vmoReader) Seek(offset int64, whence int) (int64, error) {
	offset, err := func() (int64, error) {
		switch whence {
		case io.SeekCurrent:
			return offset + int64(r.offset), nil
		case io.SeekStart:
			return offset, nil
		case io.SeekEnd:
			return offset + int64(r.size), nil
		default:
			return 0, os.ErrInvalid
		}
	}()
	if err != nil {
		return 0, err
	}

	if offset < 0 {
		return 0, os.ErrInvalid
	}

	r.offset = uint64(offset)
	return offset, nil
}

func (r *vmoReader) Close() error {
	vmo := r.vmo
	r.vmo = zx.VMO(zx.HandleInvalid)
	return vmo.Close()
}
