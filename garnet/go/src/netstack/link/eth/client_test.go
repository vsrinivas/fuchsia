// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"math"
	"syscall/zx"
	"testing"

	"fidl/fuchsia/hardware/ethernet"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"netstack/link/eth"
)

func TestClient_AllocForSend(t *testing.T) {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}

	d := ethernetext.Device{
		TB: t,
		GetInfoImpl: func() (ethernet.Info, error) {
			return ethernet.Info{}, nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
		SetClientNameImpl: func(string) (int32, error) {
			return int32(zx.ErrOk), nil
		},
	}

	saturateArena := func() (func() error, uint32) {
		// This value must be large enough to allow us to saturate the arena, but small enough to
		// avoid running the system out of memory (the client allocates buffers of this length).
		const txDepth = math.MaxUint16

		d := d
		d.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: txDepth,
			}, nil
		}
		d.StopImpl = func() error {
			return nil
		}
		c, err := eth.NewClient(t.Name(), "topo", &d, arena)
		if err != nil {
			t.Fatal(err)
		}
		var allocations uint32
		for c.AllocForSend() != nil {
			allocations++
		}
		if allocations >= txDepth {
			t.Fatalf("allocations = %d, txDepth = %d (want allocations < txDepth)", allocations, txDepth)
		}
		// txDepth > 1; txDepth < allocations (arena depth).
		return c.Close, allocations/2 + 1
	}
	freeArena, txDepth := saturateArena()

	c, err := func() (*eth.Client, error) {
		d := d
		d.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: txDepth,
			}, nil
		}
		return eth.NewClient(t.Name(), "topo", &d, arena)
	}()
	if err != nil {
		t.Fatal(err)
	}

	if got := c.AllocForSend(); got != nil {
		t.Fatalf("AllocForSend() = %v, want = %v (arena should be saturated)", got, nil)
	}

	if err := freeArena(); err != nil {
		t.Fatal(err)
	}

	for i := uint32(0); i < txDepth; i++ {
		if c.AllocForSend() == nil {
			t.Fatalf("AllocForSend() = nil, want non-nil (client allocations = %d/%d)", i, txDepth)
		}
	}

	if got := c.AllocForSend(); got != nil {
		t.Fatalf("AllocForSend() = %v, want = %v (client should be saturated)", got, nil)
	}
}
