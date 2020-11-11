// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package testutil

import (
	"sync/atomic"
	"syscall/zx"
	"testing"
	"unsafe"

	"gen/netstack/link/eth"

	"fidl/fuchsia/hardware/ethernet"
	ethernetext "fidlext/fuchsia/hardware/ethernet"
)

// makeEntryFifo creates a pair of handles to a FIFO of "depth" FifoEntry
// elements for use in tests. The created handles are automatically closed on
// test cleanup.
func makeEntryFifo(t *testing.T, depth uint) (zx.Handle, zx.Handle) {
	t.Helper()
	var device, client zx.Handle
	if status := zx.Sys_fifo_create(depth, uint(unsafe.Sizeof(eth.FifoEntry{})), 0, &device, &client); status != zx.ErrOk {
		t.Fatalf("failed to create fake FIFO: %s", status)
	}
	t.Cleanup(func() {
		_ = device.Close()
		_ = client.Close()
	})
	return device, client
}

// Returns an ethernetext.Device struct that implements
// ethernet.Device and can be started and stopped.
//
// Reports the passed in ethernet.Info when Device#GetInfo is called.
func MakeEthernetDevice(t *testing.T, info ethernet.Info, depth uint32) (ethernetext.Device, ethernet.Fifos) {
	t.Helper()
	clientRxFifo, deviceRxFifo := makeEntryFifo(t, uint(depth))
	clientTxFifo, deviceTxFifo := makeEntryFifo(t, uint(depth))

	var started uint32
	return ethernetext.Device{
			TB:                t,
			GetInfoImpl:       func() (ethernet.Info, error) { return info, nil },
			SetClientNameImpl: func(string) (int32, error) { return 0, nil },
			GetStatusImpl: func() (ethernet.DeviceStatus, error) {
				var status ethernet.DeviceStatus
				if atomic.LoadUint32(&started) == 1 {
					status |= ethernet.DeviceStatusOnline
				}
				// Emulate the driver by clearing the signal. Note that without doing this the client would
				// hot-loop, continually observing the signal.
				err := deviceRxFifo.SignalPeer(zx.Signals(ethernet.SignalStatus), 0)
				return status, err
			},
			GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
				return int32(zx.ErrOk), &ethernet.Fifos{
					Rx:      clientRxFifo,
					Tx:      clientTxFifo,
					RxDepth: depth,
					TxDepth: depth,
				}, nil
			},
			SetIoBufferImpl: func(h zx.VMO) (int32, error) {
				return int32(zx.ErrOk), h.Close()
			},
			StartImpl: func() (int32, error) {
				if atomic.CompareAndSwapUint32(&started, 0, 1) {
					switch err := deviceRxFifo.SignalPeer(0, zx.Signals(ethernet.SignalStatus)).(type) {
					case *zx.Error:
						return int32(err.Status), nil
					default:
						return int32(zx.ErrOk), err
					}
				}
				return int32(zx.ErrOk), nil
			},
			ConfigMulticastSetPromiscuousModeImpl: func(bool) (int32, error) {
				return int32(zx.ErrOk), nil
			},
			StopImpl: func() error {
				atomic.StoreUint32(&started, 0)
				return nil
			},
		}, ethernet.Fifos{
			Rx: deviceRxFifo,
			Tx: deviceTxFifo,
		}
}
