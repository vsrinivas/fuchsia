// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package vmobuffer

import (
	"io"
	"os"
	"syscall/zx"

	"go.fuchsia.dev/fuchsia/src/lib/component"
)

var _ io.WriteCloser = (*VMOBuffer)(nil)

// VMOBuffer implements a self-resizing io.WriteCloser backed by a VMO.
type VMOBuffer struct {
	vmo    zx.VMO
	size   uint64
	offset uint64
}

func NewVMOBuffer(size uint64, name string) (*VMOBuffer, error) {
	vmo, err := zx.NewVMO(size, zx.VMOOptionResizable)
	if err != nil {
		return nil, err
	}
	if err := vmo.Handle().SetProperty(zx.PropName, []byte(name)); err != nil {
		vmo.Close()
		return nil, err
	}
	return &VMOBuffer{vmo: vmo, size: size, offset: 0}, nil
}

func (b *VMOBuffer) grow() error {
	newSize := b.size * 2
	if err := b.vmo.SetSize(newSize); err != nil {
		return err
	}
	b.size = newSize
	return nil
}

func (b *VMOBuffer) Write(p []byte) (int, error) {
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

func (b *VMOBuffer) Close() error {
	return b.vmo.Close()
}

func (b *VMOBuffer) GetVMO() *zx.VMO {
	return &b.vmo
}

// ToVMOReader converts the VMOBuffer into a VMOReader that outputs the data
// that has been written into the VMOBuffer. The VMOBuffer should be considered
// to be consumed after calling ToVMOReader(), and it is not valid to perform
// operations on it afterward.
func (b *VMOBuffer) ToVMOReader() (*VMOReader, error) {
	if b.vmo == zx.VMO(zx.HandleInvalid) {
		return nil, os.ErrClosed
	}
	if err := b.vmo.SetSize(b.offset); err != nil {
		return nil, err
	}

	vmo := b.vmo
	b.vmo = zx.VMO(zx.HandleInvalid)

	return &VMOReader{
		vmo:    vmo,
		size:   b.offset,
		offset: 0,
	}, nil
}

// VMOReader implements a component.Reader backed by a VMO.
type VMOReader struct {
	vmo    zx.VMO
	size   uint64
	offset uint64
}

var _ component.Reader = (*VMOReader)(nil)

func NewVMOReader(vmo zx.VMO, size uint64) *VMOReader {
	return &VMOReader{vmo: vmo, size: size, offset: 0}
}

func (r *VMOReader) Read(p []byte) (int, error) {
	n, err := r.ReadAt(p, int64(r.offset))
	r.offset += uint64(n)
	return n, err
}

func (r *VMOReader) ReadAt(p []byte, off int64) (int, error) {
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

func (r *VMOReader) Seek(offset int64, whence int) (int64, error) {
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

func (r *VMOReader) Close() error {
	return r.vmo.Close()
}

func (r *VMOReader) Size() uint64 {
	return r.size
}

func (r *VMOReader) GetVMO() *zx.VMO {
	return &r.vmo
}
