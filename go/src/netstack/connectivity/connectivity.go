// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package connectivity

import (
	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"
	"log"
	"netstack/fidlconv"
	"netstack/netiface"
	"sync"
)

var Service *net.ConnectivityService = &net.ConnectivityService{}
var reachable bool = false
var mu sync.Mutex
var debug = false

// TODO(NET-1001): extract into a separate reachability service based on a
// better network reachability signal.
func InferAndNotify(ifs []netstack.NetInterface) {
	if debug {
		log.Printf("inferring network reachability")
	}
	mu.Lock()
	current := inferReachability(ifs)
	if current != reachable {
		if debug {
			log.Printf("notifying clients of new reachability status: %t", current)
		}
		reachable = current
		notify(reachable)
	}
	mu.Unlock()
}

func CurrentlyReachable() bool {
	return reachable
}

func hasDHCPAddress(nic netstack.NetInterface) bool {
	return nic.Flags&netstack.NetInterfaceFlagDhcp != 0 && nic.Flags&netstack.NetInterfaceFlagUp != 0 && !netiface.IsAny(fidlconv.NetAddressToTCPIPAddress(nic.Addr))
}

func inferReachability(ifs []netstack.NetInterface) bool {
	for _, nic := range ifs {
		if hasDHCPAddress(nic) {
			return true
		}
	}
	return false
}

func notify(reachable bool) {
	for key, _ := range Service.Bindings {
		if p, ok := Service.EventProxyFor(key); ok {
			p.OnNetworkReachable(reachable)
		}
	}
}
