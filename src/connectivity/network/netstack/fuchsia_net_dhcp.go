// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"

	"fidl/fuchsia/net/dhcp"

	"gvisor.dev/gvisor/pkg/tcpip"
)

type clientImpl struct {
	ns    *Netstack
	nicid tcpip.NICID
}

var _ dhcp.Client = (*clientImpl)(nil)

func (di *clientImpl) Start() (dhcp.ClientStartResult, error) {
	var r dhcp.ClientStartResult

	nicInfo, ok := di.ns.stack.NICInfo()[di.nicid]
	if !ok {
		// The interface this client represents no longer exists; error so the bindings close our end of the channel.
		return r, fmt.Errorf("NIC ID %d no longer present", di.nicid)
	}

	ifState := nicInfo.Context.(*ifState)
	ifState.setDHCPStatus(nicInfo.Name, true)

	r.SetResponse(dhcp.ClientStartResponse{})
	return r, nil
}

func (di *clientImpl) Stop() (dhcp.ClientStopResult, error) {
	r := dhcp.ClientStopResult{}

	nicInfo, ok := di.ns.stack.NICInfo()[di.nicid]
	if !ok {
		// The interface this client represents no longer exists; error so the bindings close our end of the channel.
		return r, fmt.Errorf("NIC ID %d no longer present", di.nicid)
	}

	ifState := nicInfo.Context.(*ifState)
	ifState.setDHCPStatus(nicInfo.Name, false)

	r.SetResponse(dhcp.ClientStopResponse{})
	return r, nil
}
