// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"math"
	"syscall/zx"
	"testing"

	"fidl/zircon/ethernet"

	"netstack/link/eth"
)

type device struct {
	testing.TB

	getInfo                           func() (ethernet.Info, error)
	getFifos                          func() (int32, *ethernet.Fifos, error)
	setIoBuffer                       func(zx.VMO) (int32, error)
	start                             func() (int32, error)
	stop                              func() error
	listenStart                       func() (int32, error)
	listenStop                        func() error
	setClientName                     func(string) (int32, error)
	getStatus                         func() (uint32, error)
	setPromiscuousMode                func(bool) (int32, error)
	configMulticastAddMac             func(addr ethernet.MacAddress) (int32, error)
	configMulticastDeleteMac          func(addr ethernet.MacAddress) (int32, error)
	configMulticastSetPromiscuousMode func(enabled bool) (int32, error)
	configMulticastTestFilter         func() (int32, error)
	dumpRegisters                     func() (int32, error)
}

func (d *device) GetInfo() (ethernet.Info, error) {
	fn := d.getInfo
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to GetInfo")
	}
	return fn()
}

func (d *device) GetFifos() (int32, *ethernet.Fifos, error) {
	fn := d.getFifos
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to GetFifos")
	}
	return fn()
}

func (d *device) SetIoBuffer(h zx.VMO) (int32, error) {
	fn := d.setIoBuffer
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to SetIoBuffer")
	}
	return fn(h)
}

func (d *device) Start() (int32, error) {
	fn := d.start
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to Start")
	}
	return fn()
}

func (d *device) Stop() error {
	fn := d.stop
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to Stop")
	}
	return fn()
}

func (d *device) ListenStart() (int32, error) {
	fn := d.listenStart
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to ListenStart")
	}
	return fn()
}

func (d *device) ListenStop() error {
	fn := d.listenStop
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to ListenStop")
	}
	return fn()
}

func (d *device) SetClientName(name string) (int32, error) {
	fn := d.setClientName
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to SetClientName")
	}
	return fn(name)
}

func (d *device) GetStatus() (uint32, error) {
	fn := d.getStatus
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to GetStatus")
	}
	return fn()
}

func (d *device) SetPromiscuousMode(enabled bool) (int32, error) {
	fn := d.setPromiscuousMode
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to SetPromiscuousMode")
	}
	return fn(enabled)
}

func (d *device) ConfigMulticastAddMac(addr ethernet.MacAddress) (int32, error) {
	fn := d.configMulticastAddMac
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to ConfigMulticastAddMac")
	}
	return fn(addr)
}

func (d *device) ConfigMulticastDeleteMac(addr ethernet.MacAddress) (int32, error) {
	fn := d.configMulticastDeleteMac
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to ConfigMulticastDeleteMac")
	}
	return fn(addr)
}

func (d *device) ConfigMulticastSetPromiscuousMode(enabled bool) (int32, error) {
	fn := d.configMulticastSetPromiscuousMode
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to ConfigMulticastSetPromiscuousMode")
	}
	return fn(enabled)
}

func (d *device) ConfigMulticastTestFilter() (int32, error) {
	fn := d.configMulticastTestFilter
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to ConfigMulticastTestFilter")
	}
	return fn()
}

func (d *device) DumpRegisters() (int32, error) {
	fn := d.dumpRegisters
	if fn == nil {
		d.Helper()
		d.Fatal("unexpected call to DumpRegisters")
	}
	return fn()
}

func TestClient_AllocForSend(t *testing.T) {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}

	d := device{
		TB: t,
		getInfo: func() (ethernet.Info, error) {
			return ethernet.Info{}, nil
		},
		setIoBuffer: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		start: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
		setClientName: func(string) (int32, error) {
			return int32(zx.ErrOk), nil
		},
	}

	saturateArena := func() (func(), uint32) {
		// This value must be large enough to allow us to saturate the arena, but small enough to
		// avoid running the system out of memory (the client allocates buffers of this length).
		const txDepth = math.MaxUint16

		d := d
		d.getFifos = func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: txDepth,
			}, nil
		}
		d.stop = func() error {
			return nil
		}
		c, err := eth.NewClient(t.Name(), "topo", &d, arena, nil)
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
		d.getFifos = func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: txDepth,
			}, nil
		}
		return eth.NewClient(t.Name(), "topo", &d, arena, func(eth.State) {})
	}()
	if err != nil {
		t.Fatal(err)
	}

	if got := c.AllocForSend(); got != nil {
		t.Fatalf("AllocForSend() = %v, want = %v (arena should be saturated)", got, nil)
	}

	freeArena()

	for i := uint32(0); i < txDepth; i++ {
		if c.AllocForSend() == nil {
			t.Fatalf("AllocForSend() = nil, want non-nil (client allocations = %d/%d)", i, txDepth)
		}
	}

	if got := c.AllocForSend(); got != nil {
		t.Fatalf("AllocForSend() = %v, want = %v (client should be saturated)", got, nil)
	}
}
