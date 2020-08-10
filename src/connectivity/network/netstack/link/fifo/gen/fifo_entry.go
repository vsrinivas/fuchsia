// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

// #include <zircon/device/ethernet.h>
// #include <zircon/types.h>
import "C"
import (
	"fmt"
)

const FifoRXOK = C.ETH_FIFO_RX_OK
const FifoTXOK = C.ETH_FIFO_TX_OK
const FifoInvalid = C.ETH_FIFO_INVALID
const FifoTXRX = C.ETH_FIFO_RX_TX

const FifoMaxSize = C.ZX_FIFO_MAX_SIZE_BYTES
const cookieMagic = 0x42420102 // used to fill top 32-bits of FifoEntry.cookie

type FifoEntry = C.struct_eth_fifo_entry

func (e *FifoEntry) Index() int32 {
	if e.cookie>>32 != cookieMagic {
		panic(fmt.Sprintf("buffer entry has bad cookie: %x", e.cookie))
	}
	return int32(e.cookie)
}

func (e *FifoEntry) Length() uint16 {
	return uint16(e.length)
}

func (e *FifoEntry) SetLength(length int) {
	e.length = C.uint16_t(length)
}

func (e *FifoEntry) Flags() uint16 {
	return uint16(e.flags)
}

func NewFifoEntry(offset uint32, length uint16, index int32) FifoEntry {
	return FifoEntry{
		offset: C.uint32_t(offset),
		length: C.uint16_t(length),
		cookie: (cookieMagic << 32) | C.uint64_t(index),
	}
}
