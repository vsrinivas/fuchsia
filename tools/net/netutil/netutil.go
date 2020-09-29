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
// the advertisement message sent from the node.
func GetAdvertisement(ctx context.Context, nodename string) (*net.UDPAddr, *netboot.Advertisement, *net.UDPConn, error) {
	// Retry, as the netstack might not yet be up.
	var addr *net.UDPAddr
	var msg *netboot.Advertisement
	var conn *net.UDPConn
	var err error
	n := netboot.NewClient(time.Second)
	err = retry.Retry(ctx, retry.WithMaxDuration(retry.NewConstantBackoff(5*time.Second), time.Minute), func() error {
		addr, msg, conn, err = n.BeaconForNodename(ctx, nodename, nodename != netboot.NodenameWildcard)
		return err
	}, nil)
	if err != nil {
		return addr, msg, conn, fmt.Errorf("%s %q: %v", constants.CannotFindNodeErrMsg, nodename, err)
	}
	return addr, msg, conn, nil
}
