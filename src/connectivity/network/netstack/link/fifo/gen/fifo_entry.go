// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package eth

import (
	// #include <zircon/device/ethernet.h>
	// #include <zircon/types.h>
	"C"
)

const FifoRXOK = C.ETH_FIFO_RX_OK
const FifoTXOK = C.ETH_FIFO_TX_OK
const FifoInvalid = C.ETH_FIFO_INVALID
const FifoTXRX = C.ETH_FIFO_RX_TX

const FifoMaxSize = C.ZX_FIFO_MAX_SIZE_BYTES

type FifoEntry = C.struct_eth_fifo_entry

func (e *FifoEntry) Offset() uint32 {
	return uint32(e.offset)
}

func (e *FifoEntry) Length() uint16 {
	return uint16(e.length)
}

func (e *FifoEntry) SetLength(length uint16) {
	e.length = C.uint16_t(length)
}

func (e *FifoEntry) Flags() uint16 {
	return uint16(e.flags)
}

func MakeFifoEntry(offset uint32, length uint16) FifoEntry {
	return FifoEntry{
		offset: C.uint32_t(offset),
		length: C.uint16_t(length),
	}
}
