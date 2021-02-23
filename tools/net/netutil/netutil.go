// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netutil

import (
	"context"
	"fmt"
	"net"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
	"go.fuchsia.dev/fuchsia/tools/net/netutil/constants"
)

// GetNodeAddress returns the netsvc address of nodename.
func GetNodeAddress(ctx context.Context, nodename string) (*net.UDPAddr, error) {
	// Retry, as the netstack might not yet be up.
	var addr *net.UDPAddr
	var err error
	n := netboot.NewClient(time.Second)
	err = retry.Retry(ctx, retry.WithMaxDuration(&retry.ZeroBackoff{}, time.Minute), func() error {
		addr, err = n.Discover(ctx, nodename)
		return err
	}, nil)
	if err != nil {
		return nil, fmt.Errorf("%s %q: %v", constants.CannotFindNodeErrMsg, nodename, err)
	}
	return addr, nil
}

// GetAdvertisement returns the netsvc address for the given node along with
// the advertisement message sent from the node. If both nodename and ipv6Addr
// are provided, GetAdvertisement will listen for a node that matches both.
func GetAdvertisement(ctx context.Context, nodename string, ipv6Addr *net.UDPAddr) (*net.UDPAddr, *netboot.Advertisement, func(), error) {
	// Retry, as the netstack might not yet be up.
	var addr *net.UDPAddr
	var msg *netboot.Advertisement
	var cleanup func()
	var err error
	n := netboot.NewClient(time.Second)
	err = retry.Retry(ctx, retry.WithMaxDuration(retry.NewConstantBackoff(5*time.Second), time.Minute), func() error {
		// Reuse port if a specific nodename or ip address is provided so multiple
		// bootservers can be run at the same time to pave different devices.
		reusable := nodename != netboot.NodenameWildcard || ipv6Addr != nil
		addr, msg, cleanup, err = n.BeaconForDevice(ctx, nodename, ipv6Addr, reusable)
		return err
	}, nil)
	if err != nil {
		return addr, msg, cleanup, fmt.Errorf("%s %q: %v", constants.CannotFindNodeErrMsg, nodename, err)
	}
	return addr, msg, cleanup, nil
}
