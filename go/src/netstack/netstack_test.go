package main

import (
	"fidl/fuchsia/netstack"
	"fidl/zircon/ethernet"
	ethernetext "fidlext/zircon/ethernet"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
	"netstack/link/eth"
	"syscall/zx"
	"testing"
)

const (
	testDeviceName string = "testdevice"
	testTopoPath   string = "/fake/ethernet/device"
)

func TestNicName(t *testing.T) {
	ns := newNetstack(t)

	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName}, &ethernetext.Device{
		TB:                t,
		GetInfoImpl:       func() (ethernet.Info, error) { return ethernet.Info{}, nil },
		SetClientNameImpl: func(string) (int32, error) { return 0, nil },
		GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: 1,
			}, nil
		},
		GetStatusImpl: func() (uint32, error) {
			return uint32(eth.LinkUp), nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
	})

	if err != nil {
		t.Fatal(err)
	}
	if ifs.nic.Name != testDeviceName {
		t.Errorf("ifs.nic.Name = %v, want %v", ifs.nic.Name, testDeviceName)
	}
}

func TestInitialDhcpConfiguration(t *testing.T) {
	ns := newNetstack(t)

	ipAddressConfig := netstack.IpAddressConfig{}
	ipAddressConfig.SetDhcp(true)

	d := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)

	if err != nil {
		t.Fatal(err)
	}
	if ifs.dhcpState.client == nil {
		t.Errorf("no dhcp client")
	}
	if ifs.dhcpState.enabled == false {
		t.Errorf("dhcp disabled")
	}
}

func newNetstack(t *testing.T) *Netstack {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	ns := &Netstack{
		arena:    arena,
		ifStates: make(map[tcpip.NICID]*ifState),
	}
	ns.mu.stack = stack.New(
		[]string{
			ipv4.ProtocolName,
			ipv6.ProtocolName,
			arp.ProtocolName,
		}, nil, stack.Options{})

	OnInterfacesChanged = func() {}
	return ns
}

// Returns an ethernetext.Device struct that implements
// ethernet.Device and can be started and stopped.
//
// Reports the passed in ethernet.Info when Device#GetInfo is called.
func deviceForAddEth(info ethernet.Info, t *testing.T) ethernetext.Device {
	return ethernetext.Device{
		TB: t,
		GetInfoImpl: func() (ethernet.Info, error) {
			return info, nil
		},
		SetClientNameImpl: func(string) (int32, error) { return 0, nil },
		GetStatusImpl: func() (uint32, error) {
			return uint32(eth.LinkUp), nil
		},
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
		StopImpl: func() error {
			return nil
		},
	}
}
