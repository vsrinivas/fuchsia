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
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
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

func (ni *netstackImpl) GetRouteTable(fidl.Context) ([]netstack.RouteTableEntry, error) {
	table := ni.ns.GetExtendedRouteTable()
	out := make([]netstack.RouteTableEntry, 0, len(table))
	for _, e := range table {
		var gatewayPtr *fidlnet.IpAddress
		if len(e.Route.Gateway) != 0 {
			gateway := fidlconv.ToNetIpAddress(e.Route.Gateway)
			gatewayPtr = &gateway
		}
		out = append(out, netstack.RouteTableEntry{
			Destination: fidlnet.Subnet{
				Addr:      fidlconv.ToNetIpAddress(e.Route.Destination.ID()),
				PrefixLen: uint8(e.Route.Destination.Prefix()),
			},
			Gateway: gatewayPtr,
			Nicid:   uint32(e.Route.NIC),
			Metric:  uint32(e.Metric),
		})
	}
	return out, nil
}

func routeToNs(r netstack.RouteTableEntry) tcpip.Route {
	route := tcpip.Route{
		Destination: fidlconv.ToTCPIPSubnet(r.Destination),
		NIC:         tcpip.NICID(r.Nicid),
	}
	if g := r.Gateway; g != nil {
		route.Gateway = fidlconv.ToTCPIPAddress(*g)
	}
	return route
}

type routeTableTransactionImpl struct {
	ni *netstackImpl
}

func (i *routeTableTransactionImpl) AddRoute(_ fidl.Context, r netstack.RouteTableEntry) (int32, error) {
	err := i.ni.ns.AddRoute(routeToNs(r), routes.Metric(r.Metric), false /* not dynamic */)
	if err != nil {
		return int32(zx.ErrInvalidArgs), err
	}
	return int32(zx.ErrOk), nil
}

func (i *routeTableTransactionImpl) DelRoute(_ fidl.Context, r netstack.RouteTableEntry) (int32, error) {
	err := i.ni.ns.DelRoute(routeToNs(r))
	if err != nil {
		return int32(zx.ErrInvalidArgs), err
	}
	return int32(zx.ErrOk), nil
}

func (ni *netstackImpl) StartRouteTableTransaction(_ fidl.Context, req netstack.RouteTableTransactionWithCtxInterfaceRequest) (int32, error) {
	{
		ni.ns.mu.Lock()
		defer ni.ns.mu.Unlock()

		if ni.ns.mu.transactionRequest != nil {
			oldChannel := ni.ns.mu.transactionRequest.Channel
			var observed zx.Signals
			if status := zx.Sys_object_wait_one(zx.Handle(oldChannel), zx.SignalChannelReadable|zx.SignalChannelWritable, 0, &observed); status != zx.ErrOk {
				_ = syslog.WarnTf("fuchsia.netstack.Netstack/StartRouteTableTransaction", "zx.Sys_object_wait_one(_, zx.SignalChannelReadable|zx.SignalChannelWritable, _, _): %s", status)
			}
			// If the channel is neither readable nor writable, there is no
			// data left to be processed (not readable) and we can't return
			// any more results (not writable).  It's not enough to only
			// look at peerclosed because the peer can close the channel
			// while it still has data in its buffers.
			if observed&(zx.SignalChannelReadable|zx.SignalChannelWritable) == 0 {
				ni.ns.mu.transactionRequest = nil
			}
		}
		if ni.ns.mu.transactionRequest != nil {
			return int32(zx.ErrShouldWait), nil
		}
		ni.ns.mu.transactionRequest = &req
	}
	stub := netstack.RouteTableTransactionWithCtxStub{Impl: &routeTableTransactionImpl{ni: ni}}
	go component.ServeExclusive(context.Background(), &stub, req.Channel, func(err error) {
		// NB: this protocol is not discoverable, so the bindings do not include its name.
		_ = syslog.WarnTf("fuchsia.netstack.RouteTableTransaction", "%s", err)
	})
	return int32(zx.ErrOk), nil
}

// Add address to the given network interface.
func (ni *netstackImpl) SetInterfaceAddress(_ fidl.Context, nicid uint32, address fidlnet.IpAddress, prefixLen uint8) (netstack.NetErr, error) {
	protocolAddr := toProtocolAddr(fidlnet.Subnet{
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
	protocolAddr := toProtocolAddr(fidlnet.Subnet{
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

// SetInterfaceMetric changes the metric for an interface and updates all
// routes tracking that interface metric. This takes the lock.
func (ni *netstackImpl) SetInterfaceMetric(_ fidl.Context, nicid uint32, metric uint32) (result netstack.NetErr, err error) {
	_ = syslog.Infof("update interface metric for NIC %d to metric=%d", nicid, metric)

	nic := tcpip.NICID(nicid)
	m := routes.Metric(metric)

	nicInfo, ok := ni.ns.stack.NICInfo()[nic]
	if !ok {
		return netstack.NetErr{Status: netstack.StatusUnknownInterface}, nil
	}

	ifState := nicInfo.Context.(*ifState)
	ifState.updateMetric(m)

	ni.ns.routeTable.UpdateMetricByInterface(nic, m)
	ni.ns.routeTable.UpdateStack(ni.ns.stack)
	return netstack.NetErr{Status: netstack.StatusOk}, nil
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

func (ni *netstackImpl) SetInterfaceStatus(_ fidl.Context, nicid uint32, enabled bool) error {
	if nicInfo, ok := ni.ns.stack.NICInfo()[tcpip.NICID(nicid)]; ok {
		if err := nicInfo.Context.(*ifState).setState(enabled); err != nil {
			_ = syslog.Errorf("(NIC %d).setState(enabled=%t): %s", nicid, enabled, err)
		}
	} else {
		_ = syslog.Warnf("(NIC %d).setState(enabled=%t): not found", nicid, enabled)
	}
	return nil
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
	go component.ServeExclusive(context.Background(), &stub, request.Channel, func(err error) {
		// NB: this protocol is not discoverable, so the bindings do not include its name.
		_ = syslog.WarnTf("fuchsia.net.dhcp.Client", "%s", err)
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
