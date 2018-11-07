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
	"netstack/fidlconv"
	"netstack/link/eth"
	"syscall/zx"
	"testing"
)

const (
	testDeviceName string        = "testdevice"
	testTopoPath   string        = "/fake/ethernet/device"
	testIpAddress  tcpip.Address = tcpip.Address("\xc0\xa8\x2a\x10")
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

func TestDhcpConfiguration(t *testing.T) {
	ns := newNetstack(t)

	ipAddressConfig := netstack.IpAddressConfig{}
	ipAddressConfig.SetDhcp(true)

	d := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)

	if err != nil {
		t.Fatal(err)
	}

	if !ifs.dhcpState.configured {
		t.Errorf("expected dhcp to be configured initially")
	}
	if !ifs.dhcpState.Running() {
		t.Errorf("expected dhcp client to be running initially")
	}

	ifs.eth.Down()
	if ifs.dhcpState.Running() {
		t.Errorf("expected dhcp client to be stopped on eth down")
	}
	if !ifs.dhcpState.configured {
		t.Errorf("expected dhcp configuration to be preserved on eth down")
	}

	ifs.eth.Up()
	if !ifs.dhcpState.Running() {
		t.Errorf("expected dhcp client to be running on eth restart")
	}
	if !ifs.dhcpState.configured {
		t.Errorf("expected dhcp configuration to be preserved on eth restart")
	}
}

func TestStaticIPConfiguration(t *testing.T) {
	ns := newNetstack(t)

	ipAddressConfig := netstack.IpAddressConfig{}
	addr := fidlconv.ToNetAddress(testIpAddress)
	subnet := netstack.Subnet{Addr: addr, PrefixLen: 32}
	ipAddressConfig.SetStaticIp(subnet)
	d := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)

	if err != nil {
		t.Fatal(err)
	}

	if ifs.nic.Addr != testIpAddress {
		t.Errorf("expected static IP to be set when configured")
	}
	if ifs.dhcpState.configured {
		t.Errorf("expected dhcp state to be disabled initially")
	}

	ifs.eth.Down()

	if ifs.dhcpState.configured {
		t.Errorf("expected dhcp state to remain disabled after bringing interface down")
	}

	ifs.eth.Up()

	if ifs.dhcpState.configured {
		t.Errorf("expected dhcp state to remain disabled after restarting interface")
	}

	ifs.setDHCPStatus(true)

	if !ifs.dhcpState.configured {
		t.Errorf("expected dhcp state to become configured after manually enabling it")
	}
	if !ifs.dhcpState.Running() {
		t.Errorf("expected dhcp state running")
	}
}

// Regression test.
func TestWLANStaticIPConfiguration(t *testing.T) {
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

	ipAddressConfig := netstack.IpAddressConfig{}
	addr := fidlconv.ToNetAddress(testIpAddress)
	subnet := netstack.Subnet{Addr: addr, PrefixLen: 32}
	ipAddressConfig.SetStaticIp(subnet)
	d := deviceForAddEth(ethernet.Info{Features: ethernet.InfoFeatureWlan}, t)
	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)
	if err != nil {
		t.Fatal(err)
	}

	if ifs.nic.Addr != testIpAddress {
		t.Errorf("expected static IP to be set when configured")
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
