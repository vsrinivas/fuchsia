package main

import (
	"fidl/fuchsia/netstack"
	"fidl/zircon/ethernet"
	ethernetext "fidlext/zircon/ethernet"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/stack"
	"netstack/link/eth"
	"syscall/zx"
	"testing"
)

func TestRunning(t *testing.T) {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	ns := &Netstack{
		arena:    arena,
		ifStates: make(map[tcpip.NICID]*ifState),
	}
	ns.mu.stack = stack.New(nil, nil, stack.Options{})

	OnInterfacesChanged = func() {}
	ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{}, &ethernetext.Device{
		TB:                t,
		GetInfoImpl:       func() (ethernet.Info, error) { return ethernet.Info{}, nil },
		SetClientNameImpl: func(string) (int32, error) { return 0, nil },
		GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: 1,
			}, nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
	})
}
