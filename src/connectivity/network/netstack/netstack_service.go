// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"errors"
	"fmt"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/hardware/ethernet"
	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/dhcp"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
)

type netstackImpl struct {
	ns *Netstack
}

// Add address to the given network interface.
func (ni *netstackImpl) SetInterfaceAddress(_ fidl.Context, nicid uint32, address fidlnet.IpAddress, prefixLen uint8) (netstack.NetErr, error) {
	protocolAddr := fidlconv.ToTCPIPProtocolAddress(fidlnet.Subnet{
		Addr:      address,
		PrefixLen: prefixLen,
	})
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		return netstack.NetErr{Status: netstack.StatusParseError, Message: "prefix length exceeds address length"}, nil
	}

	switch status := ni.ns.addInterfaceAddress(tcpip.NICID(nicid), protocolAddr, true /* addRoute */); status {
	case zx.ErrOk:
		return netstack.NetErr{Status: netstack.StatusOk}, nil
	case zx.ErrNotFound:
		return netstack.NetErr{Status: netstack.StatusUnknownInterface}, nil
	case zx.ErrAlreadyExists:
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: status.String()}, nil
	default:
		panic(fmt.Sprintf("NIC %d: failed to add address %s: %s", nicid, protocolAddr.AddressWithPrefix, status))
	}
}

func (ni *netstackImpl) RemoveInterfaceAddress(_ fidl.Context, nicid uint32, address fidlnet.IpAddress, prefixLen uint8) (netstack.NetErr, error) {
	protocolAddr := fidlconv.ToTCPIPProtocolAddress(fidlnet.Subnet{
		Addr:      address,
		PrefixLen: prefixLen,
	})
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		return netstack.NetErr{Status: netstack.StatusParseError, Message: "prefix length exceeds address length"}, nil
	}

	switch status := ni.ns.removeInterfaceAddress(tcpip.NICID(nicid), protocolAddr, true /* removeRoute */); status {
	case zx.ErrOk:
		return netstack.NetErr{Status: netstack.StatusOk}, nil
	case zx.ErrNotFound:
		// NB: This is inaccurate since it could be the address that was not found,
		// but will not be fixed since this API has been deprecated in favor of
		// `fuchsia.net.interfaces.admin/Control.RemoveAddress`.
		return netstack.NetErr{Status: netstack.StatusUnknownInterface}, nil
	default:
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: status.String()}, nil
	}
}

func (ni *netstackImpl) BridgeInterfaces(_ fidl.Context, nicids []uint32) (netstack.NetErr, uint32, error) {
	nics := make([]tcpip.NICID, len(nicids))
	for i, n := range nicids {
		nics[i] = tcpip.NICID(n)
	}
	ifs, err := ni.ns.Bridge(nics)
	if err != nil {
		return netstack.NetErr{Status: netstack.StatusUnknownError, Message: err.Error()}, 0, nil
	}
	return netstack.NetErr{Status: netstack.StatusOk}, uint32(ifs.nicid), nil
}

func (ni *netstackImpl) GetDhcpClient(_ fidl.Context, id uint32, request dhcp.ClientWithCtxInterfaceRequest) (netstack.NetstackGetDhcpClientResult, error) {
	var result netstack.NetstackGetDhcpClientResult
	nicid := tcpip.NICID(id)
	if _, ok := ni.ns.stack.NICInfo()[nicid]; !ok {
		result.SetErr(int32(zx.ErrNotFound))
		_ = request.Close()
		return result, nil
	}
	stub := dhcp.ClientWithCtxStub{Impl: &clientImpl{ns: ni.ns, nicid: nicid}}
	go component.Serve(context.Background(), &stub, request.Channel, component.ServeOptions{
		OnError: func(err error) {
			// NB: this protocol is not discoverable, so the bindings do not include its name.
			_ = syslog.WarnTf("fuchsia.net.dhcp.Client", "%s", err)
		},
	})
	result.SetResponse(netstack.NetstackGetDhcpClientResponse{})
	return result, nil
}

func (ni *netstackImpl) AddEthernetDevice(_ fidl.Context, topopath string, interfaceConfig netstack.InterfaceConfig, device ethernet.DeviceWithCtxInterface) (netstack.NetstackAddEthernetDeviceResult, error) {
	var result netstack.NetstackAddEthernetDeviceResult
	if ifs, err := ni.ns.addEth(topopath, interfaceConfig, &device); err != nil {
		var tcpipErr *TcpIpError
		if errors.As(err, &tcpipErr) {
			result.SetErr(int32(tcpipErr.ToZxStatus()))
		} else {
			result.SetErr(int32(zx.ErrInternal))
		}
	} else {
		result.SetResponse(netstack.NetstackAddEthernetDeviceResponse{Nicid: uint32(ifs.nicid)})
	}
	return result, nil
}
