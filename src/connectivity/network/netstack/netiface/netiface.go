// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netiface

import (
	"netstack/routes"

	"github.com/google/netstack/tcpip"
)

// TODO(NET-1223):This should be moved into netstack.go.
type NIC struct {
	IsDynamicAddr bool
	Name          string
	Features      uint32
	Metric        routes.Metric // used as a default for routes that use this NIC
	DNSServers    []tcpip.Address
	Ipv6addrs     []tcpip.Address
}
