// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"sort"

	"app/context"
	"netstack/fidlconv"
	"netstack/link/eth"
	"syscall/zx"

	netfidl "fidl/fuchsia/net"
	stack "fidl/fuchsia/net_stack"

	"github.com/google/netstack/tcpip"
)

type stackImpl struct{}

func getInterfaceInfo(nicid tcpip.NICID, ifs *ifState) *stack.InterfaceInfo {
	// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
	broadaddr := []byte(ifs.nic.Addr)
	if len(ifs.nic.Netmask) != len(ifs.nic.Addr) {
		log.Printf("warning: mismatched netmask and address length for nic: %+v", ifs.nic)
		return nil
	}

	for i := range broadaddr {
		broadaddr[i] |= ^ifs.nic.Netmask[i]
	}

	mac := &netfidl.MacAddress{Addr: [6]uint8{}}
	copy(mac.Addr[:], ifs.nic.Mac[:])

	// TODO(tkilbourn): distinguish between enabled and link up
	var status uint32 = 0
	if ifs.state == eth.StateStarted {
		status |= stack.InterfaceStatusEnabled
	}

	features := []stack.InterfaceFeature{}
	if ifs.nic.Features&eth.FeatureWlan != 0 {
		features = append(features, stack.InterfaceFeatureWlan)
	}
	if ifs.nic.Features&eth.FeatureSynth != 0 {
		features = append(features, stack.InterfaceFeatureSynthetic)
	}
	if ifs.nic.Features&eth.FeatureLoopback != 0 {
		features = append(features, stack.InterfaceFeatureLoopback)
	}

	// TODO(tkilbourn): implement interface addresses
	addrs := []stack.InterfaceAddress{}
	if len(ifs.nic.Addr) > 0 {
		addrs = append(addrs, stack.InterfaceAddress{
			IpAddress: fidlconv.ToNetIpAddress(ifs.nic.Addr),
			PrefixLen: fidlconv.GetPrefixLen(tcpip.Address(ifs.nic.Netmask)),
		})
	}

	return &stack.InterfaceInfo{
		Id:        uint64(nicid),
		Path:      "not-supported",
		Mac:       mac,
		Mtu:       uint32(ifs.statsEP.MTU()),
		Status:    status,
		Features:  features,
		Addresses: addrs,
	}

}

func getNetInterfaces() (out []stack.InterfaceInfo) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	for nicid, ifs := range ns.ifStates {
		out = append(out, *getInterfaceInfo(nicid, ifs))
	}
	sort.Slice(out[:], func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func getInterface(id uint64) (*stack.InterfaceInfo, *stack.Error) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	for nicid, ifs := range ns.ifStates {
		if uint64(nicid) == id {
			return getInterfaceInfo(nicid, ifs), nil
		}
	}
	return nil, &stack.Error{Type: stack.ErrorTypeNotFound}
}

func setInterfaceState(id uint64, enabled bool) *stack.Error {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	var ifs *ifState = nil
	for i, ifState := range ns.ifStates {
		if uint64(i) == id {
			ifs = ifState
			break
		}
	}
	if ifs == nil {
		return &stack.Error{Type: stack.ErrorTypeNotFound}
	}

	if enabled {
		ifs.eth.Up()
	} else {
		ifs.eth.Down()
	}
	return nil
}

func (ni *stackImpl) ListInterfaces() ([]stack.InterfaceInfo, error) {
	return getNetInterfaces(), nil
}

func (ni *stackImpl) GetInterfaceInfo(id uint64) (*stack.InterfaceInfo, *stack.Error, error) {
	info, err := getInterface(id)
	return info, err, nil
}

func (ni *stackImpl) EnableInterface(id uint64) (*stack.Error, error) {
	return setInterfaceState(id, true), nil
}

func (ni *stackImpl) DisableInterface(id uint64) (*stack.Error, error) {
	return setInterfaceState(id, false), nil
}

func (ni *stackImpl) AddInterfaceAddress(id uint64, addr stack.InterfaceAddress) (*stack.Error, error) {
	panic("not implemented")
}

func (ni *stackImpl) DelInterfaceAddress(id uint64, addr netfidl.IpAddress) (*stack.Error, error) {
	panic("not implemented")
}

func (ni *stackImpl) GetForwardingTable() ([]stack.ForwardingEntry, error) {
	panic("not implemented")
}

func (ni *stackImpl) AddForwardingEntry(entry stack.ForwardingEntry) (*stack.Error, error) {
	panic("not implemented")
}

func (ni *stackImpl) DelForwardingEntry(subset netfidl.Subnet) (*stack.Error, error) {
	panic("not implemented")
}

var stackService *stack.StackService

// AddStackService registers the StackService with the application context,
// allowing it to respond to FIDL queries.
func AddStackService(ctx *context.Context) error {
	if stackService != nil {
		return fmt.Errorf("AddNetworkService must be called only once")
	}
	stackService = &stack.StackService{}
	ctx.OutgoingService.AddService(stack.StackName, func(c zx.Channel) error {
		_, err := stackService.Add(&stackImpl{}, c, nil)
		return err
	})

	return nil
}
