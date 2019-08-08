// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"sort"
	"strings"
	"syscall/zx"
	"testing"
	"time"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"netstack/dhcp"
	"netstack/dns"
	"netstack/fidlconv"
	"netstack/link/eth"
	"netstack/routes"
	"netstack/util"

	"github.com/google/go-cmp/cmp"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	tcpipstack "github.com/google/netstack/tcpip/stack"
)

const (
	testDeviceName string        = "testdevice"
	testTopoPath   string        = "/fake/ethernet/device"
	testV4Address  tcpip.Address = tcpip.Address("\xc0\xa8\x2a\x10")
	testV6Address  tcpip.Address = tcpip.Address("\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10")
)

func TestNicName(t *testing.T) {
	ns := newNetstack(t)

	eth := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth)
	if err != nil {
		t.Fatal(err)
	}
	ifs.ns.mu.Lock()
	name := ifs.ns.nameLocked(ifs.nicid)
	ifs.ns.mu.Unlock()
	if name != testDeviceName {
		t.Fatalf("ifs.mu.name = %v, want = %v", name, testDeviceName)
	}
}

func TestNotStartedByDefault(t *testing.T) {
	ns := newNetstack(t)

	startCalled := false
	eth := deviceForAddEth(ethernet.Info{}, t)
	eth.StartImpl = func() (int32, error) {
		startCalled = true
		return int32(zx.ErrOk), nil
	}

	if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth); err != nil {
		t.Fatal(err)
	}

	if startCalled {
		t.Error("expected no calls to ethernet.Device.Start by addEth")
	}
}

func TestMulticastPromiscuousModeEnabledByDefault(t *testing.T) {
	ns := newNetstack(t)

	multicastPromiscuousModeEnabled := false
	eth := deviceForAddEth(ethernet.Info{}, t)
	eth.ConfigMulticastSetPromiscuousModeImpl = func(enabled bool) (int32, error) {
		multicastPromiscuousModeEnabled = enabled
		return int32(zx.ErrOk), nil
	}

	if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth); err != nil {
		t.Fatal(err)
	}

	if !multicastPromiscuousModeEnabled {
		t.Error("expected a call to ConfigMulticastSetPromiscuousMode(true) by addEth")
	}
}

func TestDhcpConfiguration(t *testing.T) {
	ns := newNetstack(t)

	ipAddressConfig := netstack.IpAddressConfig{}
	ipAddressConfig.SetDhcp(true)

	d := deviceForAddEth(ethernet.Info{}, t)
	d.StopImpl = func() error { return nil }
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)
	if err != nil {
		t.Fatal(err)
	}

	ifs.ns.mu.Lock()
	name := ifs.ns.nameLocked(ifs.nicid)
	ifs.ns.mu.Unlock()

	ifs.mu.Lock()
	if ifs.mu.dhcp.Client == nil {
		t.Error("no dhcp client")
	}

	if ifs.mu.dhcp.enabled {
		t.Error("expected dhcp to be disabled")
	}

	if ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be stopped initially")
	}

	ifs.setDHCPStatusLocked(name, true)
	ifs.mu.Unlock()

	ifs.eth.Up()

	ifs.mu.Lock()
	if !ifs.mu.dhcp.enabled {
		t.Error("expected dhcp to be enabled")
	}

	if !ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be running")
	}
	ifs.mu.Unlock()

	ifs.eth.Down()

	ifs.mu.Lock()
	if ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be stopped on eth down")
	}
	if !ifs.mu.dhcp.enabled {
		t.Error("expected dhcp configuration to be preserved on eth down")
	}
	ifs.mu.Unlock()

	ifs.eth.Up()

	ifs.mu.Lock()
	if !ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be running on eth restart")
	}
	if !ifs.mu.dhcp.enabled {
		t.Error("expected dhcp configuration to be preserved on eth restart")
	}
	ifs.mu.Unlock()
}

func TestUniqueFallbackNICNames(t *testing.T) {
	ns := newNetstack(t)

	d1 := deviceForAddEth(ethernet.Info{}, t)
	ifs1, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d1)
	if err != nil {
		t.Fatal(err)
	}

	d2 := deviceForAddEth(ethernet.Info{}, t)
	ifs2, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d2)
	if err != nil {
		t.Fatal(err)
	}
	ns.mu.Lock()
	nicInfos := ns.mu.stack.NICInfo()
	ns.mu.Unlock()

	nicInfo1, ok := nicInfos[ifs1.nicid]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d]: %s", ifs1.nicid, tcpip.ErrUnknownNICID)
	}
	nicInfo2, ok := nicInfos[ifs2.nicid]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d]: %s", ifs2.nicid, tcpip.ErrUnknownNICID)
	}

	if nicInfo1.Name == nicInfo2.Name {
		t.Fatalf("got (%+v).Name == (%+v).Name, want non-equal", nicInfo1, nicInfo2)
	}
}

func TestStaticIPConfiguration(t *testing.T) {
	ns := newNetstack(t)

	addr := fidlconv.ToNetIpAddress(testV4Address)
	ifAddr := stack.InterfaceAddress{IpAddress: addr, PrefixLen: 32}
	d := deviceForAddEth(ethernet.Info{}, t)
	d.StopImpl = func() error { return nil }
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &d)
	if err != nil {
		t.Fatal(err)
	}

	ifs.ns.mu.Lock()
	name := ifs.ns.nameLocked(ifs.nicid)
	ifs.ns.mu.Unlock()

	if err := ns.addInterfaceAddr(uint64(ifs.nicid), ifAddr); err != nil {
		t.Fatal(err)
	}

	ifs.mu.Lock()
	if info, err := ifs.toNetInterface2Locked(); err != nil {
		t.Errorf("couldn't get interface info: %s", err)
	} else if got := fidlconv.ToTCPIPAddress(info.Addr); got != testV4Address {
		t.Errorf("got ifs.toNetInterface2Locked().Addr = %+v, want = %+v", got, testV4Address)
	}

	if ifs.mu.dhcp.enabled {
		t.Error("expected dhcp state to be disabled initially")
	}
	ifs.mu.Unlock()

	ifs.eth.Down()

	ifs.mu.Lock()
	if ifs.mu.dhcp.enabled {
		t.Error("expected dhcp state to remain disabled after bringing interface down")
	}
	if ifs.mu.dhcp.running() {
		t.Error("expected dhcp state to remain stopped after bringing interface down")
	}
	ifs.mu.Unlock()

	ifs.eth.Up()

	ifs.mu.Lock()
	if ifs.mu.dhcp.enabled {
		t.Error("expected dhcp state to remain disabled after restarting interface")
	}

	ifs.setDHCPStatusLocked(name, true)
	if !ifs.mu.dhcp.enabled {
		t.Error("expected dhcp state to become enabled after manually enabling it")
	}
	if !ifs.mu.dhcp.running() {
		t.Error("expected dhcp state running")
	}
	ifs.mu.Unlock()
}

func TestWLANStaticIPConfiguration(t *testing.T) {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	ns := &Netstack{
		arena: arena,
	}
	ns.mu.ifStates = make(map[tcpip.NICID]*ifState)
	ns.mu.stack = tcpipstack.New(
		[]string{
			ipv4.ProtocolName,
			ipv6.ProtocolName,
			arp.ProtocolName,
		}, nil, tcpipstack.Options{})

	ns.OnInterfacesChanged = func([]netstack.NetInterface2) {}
	addr := fidlconv.ToNetIpAddress(testV4Address)
	ifAddr := stack.InterfaceAddress{IpAddress: addr, PrefixLen: 32}
	d := deviceForAddEth(ethernet.Info{Features: ethernet.InfoFeatureWlan}, t)
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &d)
	if err != nil {
		t.Fatal(err)
	}

	if err := ns.addInterfaceAddr(uint64(ifs.nicid), ifAddr); err != nil {
		t.Fatal(err)
	}

	if info, err := ifs.toNetInterface2Locked(); err != nil {
		t.Errorf("couldn't get interface info: %s", err)
	} else if got := fidlconv.ToTCPIPAddress(info.Addr); got != testV4Address {
		t.Errorf("got ifs.toNetInterface2Locked().Addr = %+v, want = %+v", got, testV4Address)
	}
}

func newNetstack(t *testing.T) *Netstack {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	ns := &Netstack{
		arena: arena,
	}
	ns.mu.ifStates = make(map[tcpip.NICID]*ifState)
	ns.mu.stack = tcpipstack.New(
		[]string{
			ipv4.ProtocolName,
			ipv6.ProtocolName,
			arp.ProtocolName,
		}, nil, tcpipstack.Options{})

	// We need to initialize the DNS client, since adding/removing interfaces
	// sets the DNS servers on that interface, which requires that dnsClient
	// exist.
	ns.dnsClient = dns.NewClient(ns.mu.stack)
	ns.OnInterfacesChanged = func([]netstack.NetInterface2) {}
	return ns
}

func getInterfaceAddresses(t *testing.T, ni *stackImpl, nicid tcpip.NICID) []tcpip.AddressWithPrefix {
	t.Helper()

	interfaces, err := ni.ListInterfaces()
	if err != nil {
		t.Fatalf("ni.ListInterfaces() failed: %s", err)
	}

	info, found := stack.InterfaceInfo{}, false
	for _, i := range interfaces {
		if tcpip.NICID(i.Id) == nicid {
			info = i
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("couldn't find NICID=%d in %+v", nicid, interfaces)
	}

	addrs := make([]tcpip.AddressWithPrefix, 0, len(info.Properties.Addresses))
	for _, a := range info.Properties.Addresses {
		addrs = append(addrs, tcpip.AddressWithPrefix{
			Address:   fidlconv.ToTCPIPAddress(a.IpAddress),
			PrefixLen: int(a.PrefixLen),
		})
	}
	return addrs
}

func compareInterfaceAddresses(t *testing.T, got, want []tcpip.AddressWithPrefix) {
	t.Helper()
	sort.Slice(got, func(i, j int) bool { return got[i].Address < got[j].Address })
	sort.Slice(want, func(i, j int) bool { return want[i].Address < want[j].Address })
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("Interface addresses mismatch (-want +got):\n%s", diff)
	}
}

func TestNetstackImpl_GetInterfaces2(t *testing.T) {
	ns := newNetstack(t)
	ni := &netstackImpl{ns: ns}

	d := deviceForAddEth(ethernet.Info{}, t)
	if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d); err != nil {
		t.Fatal(err)
	}

	interfaces, err := ni.GetInterfaces2()
	if err != nil {
		t.Fatal(err)
	}

	if l := len(interfaces); l == 0 {
		t.Fatalf("got len(GetInterfaces2()) = %d, want != %d", l, l)
	}

	var expectedAddr net.IpAddress
	expectedAddr.SetIpv4(net.Ipv4Address{})
	for _, iface := range interfaces {
		if iface.Addr != expectedAddr {
			t.Errorf("got interface %+v, want Addr = %+v", iface, expectedAddr)
		}
		if iface.Netmask != expectedAddr {
			t.Errorf("got interface %+v, want NetMask = %+v", iface, expectedAddr)
		}
	}
}

// Test adding a list of both IPV4 and IPV6 addresses and then removing them
// again one-by-one.
func TestListInterfaceAddresses(t *testing.T) {
	ns := newNetstack(t)
	ni := &stackImpl{ns: ns}

	d := deviceForAddEth(ethernet.Info{}, t)
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatal(err)
	}

	// The call to ns.addEth() added addresses to the stack. Make sure we include
	// those in our want list.
	wantAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

	testAddresses := []tcpip.AddressWithPrefix{
		{"\x01\x01\x01\x01", 32},
		{"\x02\x02\x02\x02", 24},
		{"\x03\x03\x03\x03", 16},
		{"\x04\x04\x04\x04", 8},
		{"\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01", 128},
		{"\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02", 64},
		{"\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03", 32},
		{"\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04", 8},
	}

	t.Run("Add", func(t *testing.T) {
		for _, addr := range testAddresses {
			t.Run(fmt.Sprintf("%s/%d", addr.Address, addr.PrefixLen), func(t *testing.T) {
				ifAddr := stack.InterfaceAddress{
					IpAddress: fidlconv.ToNetIpAddress(addr.Address),
					PrefixLen: uint8(addr.PrefixLen),
				}

				if stackErr, err := ni.AddInterfaceAddress(uint64(ifState.nicid), ifAddr); stackErr != nil || err != nil {
					t.Fatalf("ni.AddInterfaceAddress(nicid=%d, addr=%+v) failed: stackErr=%v, err=%v", uint64(ifState.nicid), ifAddr, stackErr, err)
				}

				wantAddrs = append(wantAddrs, addr)
				gotAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

				compareInterfaceAddresses(t, gotAddrs, wantAddrs)
			})
		}
	})

	t.Run("Remove", func(t *testing.T) {
		for _, addr := range testAddresses {
			t.Run(fmt.Sprintf("%s/%d", addr.Address, addr.PrefixLen), func(t *testing.T) {
				ifAddr := stack.InterfaceAddress{
					IpAddress: fidlconv.ToNetIpAddress(addr.Address),
					PrefixLen: uint8(addr.PrefixLen),
				}

				if stackErr, err := ni.DelInterfaceAddress(uint64(ifState.nicid), ifAddr); stackErr != nil || err != nil {
					t.Fatalf("ni.DelInterfaceAddress(nicid=%d, addr=%+v) failed: stackErr=%v, err=%v", uint64(ifState.nicid), ifAddr, stackErr, err)
				}

				// Remove address from list.
				for i, a := range wantAddrs {
					if a == addr {
						wantAddrs = append(wantAddrs[:i], wantAddrs[i+1:]...)
						break
					}
				}
				gotAddrs := getInterfaceAddresses(t, ni, ifState.nicid)
				compareInterfaceAddresses(t, gotAddrs, wantAddrs)
			})
		}
	})
}

// Test that adding an address with one prefix and then adding the same address
// but with a different prefix will simply replace the first address.
func TestAddAddressesThenChangePrefix(t *testing.T) {
	ns := newNetstack(t)
	ni := &stackImpl{ns: ns}
	d := deviceForAddEth(ethernet.Info{}, t)
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatal(err)
	}

	// The call to ns.addEth() added addresses to the stack. Make sure we include
	// those in our want list.
	initialAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

	// Add address.
	addr := tcpip.AddressWithPrefix{"\x01\x01\x01\x01", 8}
	ifAddr := stack.InterfaceAddress{
		IpAddress: fidlconv.ToNetIpAddress(addr.Address),
		PrefixLen: uint8(addr.PrefixLen),
	}

	if stackErr, err := ni.AddInterfaceAddress(uint64(ifState.nicid), ifAddr); stackErr != nil || err != nil {
		t.Fatalf("ni.AddInterfaceAddress(nicid=%d, addr=%+v) failed: stackErr=%v, err=%v", uint64(ifState.nicid), ifAddr, stackErr, err)
	}

	wantAddrs := append(initialAddrs, addr)
	gotAddrs := getInterfaceAddresses(t, ni, ifState.nicid)
	compareInterfaceAddresses(t, gotAddrs, wantAddrs)

	// Add the same address with a different prefix.
	addr.PrefixLen *= 2
	ifAddr.PrefixLen *= 2

	if stackErr, err := ni.AddInterfaceAddress(uint64(ifState.nicid), ifAddr); stackErr != nil || err != nil {
		t.Fatalf("ni.AddInterfaceAddress(nicid=%d, addr=%+v) failed: stackErr=%v, err=%v", uint64(ifState.nicid), ifAddr, stackErr, err)
	}

	wantAddrs = append(initialAddrs, addr)
	gotAddrs = getInterfaceAddresses(t, ni, ifState.nicid)

	compareInterfaceAddresses(t, gotAddrs, wantAddrs)
}

func TestAddRouteParameterValidation(t *testing.T) {
	ns := newNetstack(t)
	d := deviceForAddEth(ethernet.Info{}, t)
	interfaceAddress, prefix := tcpip.Address("\xf0\xf0\xf0\xf0"), uint8(24)
	subnetLocalAddress := tcpip.Address("\xf0\xf0\xf0\xf1")
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatalf("got ns.addEth(_) = _, %s want = _, nil", err)
	}

	if err := ns.addInterfaceAddress(ifState.nicid, ipv4.ProtocolNumber, interfaceAddress, prefix); err != nil {
		t.Fatalf("ns.addInterfaceAddress(%d, %d, %s, %d) = %s", ifState.nicid, ipv4.ProtocolNumber, interfaceAddress, prefix, err)
	}

	tests := []struct {
		name        string
		route       tcpip.Route
		metric      routes.Metric
		dynamic     bool
		shouldPanic bool
		shouldError bool
	}{
		{
			// TODO(NET-2244): don't panic when given invalid route destinations
			name: "zero-length destination",
			route: tcpip.Route{
				Destination: tcpip.Address(""),
				Mask:        tcpip.AddressMask(header.IPv4Broadcast),
				Gateway:     testV4Address,
				NIC:         ifState.nicid,
			},
			metric:      routes.Metric(0),
			shouldPanic: true,
		},
		{
			// TODO(NET-2244): don't panic when given invalid route destinations
			name: "invalid destination",
			route: tcpip.Route{
				Destination: tcpip.Address("\xff"),
				Mask:        tcpip.AddressMask(header.IPv4Broadcast),
				Gateway:     testV4Address,
				NIC:         ifState.nicid,
			},
			metric:      routes.Metric(0),
			shouldPanic: true,
		},
		{
			name: "IPv4 destination no NIC invalid gateway",
			route: tcpip.Route{
				Destination: testV4Address,
				Mask:        tcpip.AddressMask(header.IPv4Broadcast),
				Gateway:     testV4Address,
				NIC:         0,
			},
			metric:      routes.Metric(0),
			shouldError: true,
		},
		{
			name: "IPv6 destination no NIC invalid gateway",
			route: tcpip.Route{
				Destination: testV6Address,
				Mask:        tcpip.AddressMask(strings.Repeat("\xff", 16)),
				Gateway:     testV6Address,
				NIC:         0,
			},
			metric:      routes.Metric(0),
			shouldError: true,
		},
		{
			name: "IPv4 destination no NIC valid gateway",
			route: tcpip.Route{
				Destination: testV4Address,
				Mask:        tcpip.AddressMask(header.IPv4Broadcast),
				Gateway:     subnetLocalAddress,
				NIC:         0,
			},
		},
		{
			name: "zero length gateway",
			route: tcpip.Route{
				Destination: testV4Address,
				Mask:        tcpip.AddressMask(header.IPv4Broadcast),
				Gateway:     tcpip.Address(""),
				NIC:         ifState.nicid,
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			defer func() {
				r := recover()
				if got := r != nil; got != test.shouldPanic {
					t.Logf("recover() = %v", r)
					t.Errorf("got (recover() != nil) = %t; want = %t", got, test.shouldPanic)
				}
			}()

			err := ns.AddRoute(test.route, test.metric, test.dynamic)
			if got := err != nil; got != test.shouldError {
				t.Logf("err = %v", err)
				t.Errorf("got (ns.AddRoute(_) != nil) = %t, want = %t", got, test.shouldError)
			}
		})
	}
}

func TestDHCPAcquired(t *testing.T) {
	ns := newNetstack(t)
	d := deviceForAddEth(ethernet.Info{}, t)
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatal(err)
	}

	serverAddress := []byte(testV4Address)
	serverAddress[len(serverAddress)-1]++
	gatewayAddress := serverAddress
	gatewayAddress[len(gatewayAddress)-1]++
	const defaultLeaseLength = 60 * time.Second

	tests := []struct {
		name                 string
		oldAddr, newAddr     tcpip.Address
		oldSubnet, newSubnet tcpip.Subnet
		config               dhcp.Config
		expectedRouteTable   []routes.ExtendedRoute
	}{
		{
			name:      "subnet mask provided",
			oldAddr:   "",
			newAddr:   testV4Address,
			oldSubnet: tcpip.Subnet{},
			newSubnet: func() tcpip.Subnet {
				subnet, err := tcpip.NewSubnet(util.ApplyMask(testV4Address, util.DefaultMask(testV4Address)), util.DefaultMask(testV4Address))
				if err != nil {
					t.Fatal(err)
				}
				return subnet
			}(),
			config: dhcp.Config{
				ServerAddress: tcpip.Address(serverAddress),
				Gateway:       tcpip.Address(serverAddress),
				SubnetMask:    util.DefaultMask(testV4Address),
				DNS:           []tcpip.Address{tcpip.Address(gatewayAddress)},
				LeaseLength:   defaultLeaseLength,
			},
			expectedRouteTable: []routes.ExtendedRoute{
				{
					Route: tcpip.Route{
						Destination: util.Parse("192.168.42.0"),
						Mask:        tcpip.AddressMask(util.Parse("255.255.255.0")),
						NIC:         1,
					},
					Metric:                0,
					MetricTracksInterface: true,
					Dynamic:               true,
					Enabled:               false,
				},
				{
					Route: tcpip.Route{
						Destination: util.Parse("0.0.0.0"),
						Mask:        tcpip.AddressMask(util.Parse("0.0.0.0")),
						Gateway:     util.Parse("192.168.42.18"),
						NIC:         1,
					},
					Metric:                0,
					MetricTracksInterface: true,
					Dynamic:               true,
					Enabled:               false,
				},
				{
					Route: tcpip.Route{
						Destination: util.Parse("::"),
						Mask:        tcpip.AddressMask(util.Parse("::")),
						NIC:         1,
					},
					Metric:                0,
					MetricTracksInterface: true,
					Dynamic:               true,
					Enabled:               false,
				},
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			ifState.dhcpAcquired(test.oldAddr, test.newAddr, test.oldSubnet, test.newSubnet, test.config)
			ifState.mu.Lock()
			hasDynamicAddr := ifState.mu.hasDynamicAddr
			dnsServers := ifState.mu.dnsServers
			ifState.mu.Unlock()

			if got, want := hasDynamicAddr, true; got != want {
				t.Errorf("got ifState.mu.hasDynamicAddr = %t, want = %t", got, want)
			}

			if diff := cmp.Diff(dnsServers, test.config.DNS); diff != "" {
				t.Errorf("ifState.mu.dnsServers mismatch (-want +got):\n%s", diff)
			}

			if diff := cmp.Diff(ifState.ns.GetExtendedRouteTable(), test.expectedRouteTable); diff != "" {
				t.Errorf("GetExtendedRouteTable() mismatch (-want +got):\n%s", diff)
			}

			oldAddrWithPrefix := tcpip.AddressWithPrefix{test.oldAddr, test.oldSubnet.Prefix()}
			newAddrWithPrefix := tcpip.AddressWithPrefix{test.newAddr, test.newSubnet.Prefix()}
			ns.mu.Lock()
			infoMap := ns.mu.stack.NICInfo()
			ns.mu.Unlock()
			if info, ok := infoMap[ifState.nicid]; ok {
				found := false
				for _, address := range info.ProtocolAddresses {
					if address.Protocol == ipv4.ProtocolNumber {
						switch address.AddressWithPrefix {
						case oldAddrWithPrefix:
							t.Errorf("expired address %s was not removed from NIC addresses %v", test.oldAddr, info.ProtocolAddresses)
						case newAddrWithPrefix:
							found = true
						}
					}
				}

				if !found {
					t.Errorf("new address %s was not added to NIC addresses %v", test.newAddr, info.ProtocolAddresses)
				}
			} else {
				t.Errorf("NIC %d not found in %v", ifState.nicid, infoMap)
			}
		})
	}
}

func getNetmask(prefix uint8, bits int) net.IpAddress {
	return fidlconv.ToNetIpAddress(tcpip.Address(util.CIDRMask(int(prefix), bits)))
}

// Returns an ethernetext.Device struct that implements
// ethernet.Device and can be started and stopped.
//
// Reports the passed in ethernet.Info when Device#GetInfo is called.
func deviceForAddEth(info ethernet.Info, t *testing.T) ethernetext.Device {
	return ethernetext.Device{
		TB:                t,
		GetInfoImpl:       func() (ethernet.Info, error) { return info, nil },
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
		ConfigMulticastSetPromiscuousModeImpl: func(bool) (int32, error) {
			return int32(zx.ErrOk), nil
		},
	}
}
