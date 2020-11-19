// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package fifo

import (
	"fmt"
	"reflect"
	"syscall/zx"
	"unsafe"

	"go.uber.org/multierr"
)

// MappedVMO own a VMO and its mapping into process memory. It provides common
// operations for memory mapped VMOs. Closing it will cause the mapped region to
// be unmapped.
type MappedVMO struct {
	vaddr zx.Vaddr
	len   uint64
}

// Close unmaps the mapped VMO region and closes the VMO object.
func (vmo *MappedVMO) Close() error {
	return zx.VMARRoot.Unmap(vmo.vaddr, vmo.len)
}

// GetData returns a slice view into the mapped VMO with offset and length.
// If the provided range is not within bounds of the VMO, GetData panics.
func (vmo *MappedVMO) GetData(offset, len uint64) []byte {
	if offset+len > vmo.len {
		panic(fmt.Sprintf("invalid VMO data range (offset:%d, len:%d) for VMO with %d bytes", offset, len, vmo.len))
	}
	return *(*[]byte)(unsafe.Pointer(&reflect.SliceHeader{
		Data: uintptr(vmo.vaddr) + uintptr(offset),
		Len:  int(len),
		Cap:  int(len),
	}))
}

// GetPointer returns a pointer to the mapped VMO at offset. If the offset is
// not within bounds of the mapped VMO, GetPointer panics.
func (vmo *MappedVMO) GetPointer(offset uint64) unsafe.Pointer {
	if offset > vmo.len {
		panic(fmt.Sprintf("invalid VMO pointer (offset:%d) for VMO with %d bytes", offset, vmo.len))
	}
	return unsafe.Pointer(uintptr(vmo.vaddr) + uintptr(offset))
}

// NewMappedVMO creates a new VMO the given name and size, in bytes, and maps it
// to the process' memory space.
func NewMappedVMO(size uint64, name string) (MappedVMO, zx.VMO, error) {
	vmo, err := zx.NewVMO(size, 0)
	if err != nil {
		return MappedVMO{}, zx.VMO(zx.HandleInvalid), err
	}
	if err := vmo.Handle().SetProperty(zx.PropName, []byte(name)); err != nil {
		err = multierr.Append(err, vmo.Close())
		return MappedVMO{}, zx.VMO(zx.HandleInvalid), err
	}
	mappedVmo, err := MapVMO(vmo)
	if err != nil {
		err = multierr.Append(err, vmo.Close())
		return MappedVMO{}, zx.VMO(zx.HandleInvalid), err
	}
	return mappedVmo, vmo, nil
}

/// Len returns the length of the VMO.
func (vmo *MappedVMO) Len() uint64 {
	return vmo.len
}

// MapVmo maps a vmo into the process' memory space. It does not take ownership
// of the VMO.
func MapVMO(vmo zx.VMO) (MappedVMO, error) {
	size, err := vmo.Size()
	if err != nil {
		return MappedVMO{}, err
	}
	vaddr, err := zx.VMARRoot.Map(0, vmo, 0, size, zx.VMFlagPermRead|zx.VMFlagPermWrite)
	if err != nil {
		return MappedVMO{}, err
	}

	return MappedVMO{
		vaddr: vaddr,
		len:   size,
	}, nil
}
