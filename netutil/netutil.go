// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netutil

import (
	"context"
	"fmt"
	"net"
	"time"

	"fuchsia.googlesource.com/tools/netboot"
	"fuchsia.googlesource.com/tools/retry"
)

// GetNodeAddress returns the UDP address corresponding to a given node, specifically
// the netsvc or fuchsia address dependending on the value of `fuchsia`.
func GetNodeAddress(ctx context.Context, nodename string, fuchsia bool) (*net.UDPAddr, error) {
	// Retry, as the netstack might not yet be up.
	var addr *net.UDPAddr
	var err error
	n := netboot.NewClient(time.Second)
	err = retry.Retry(ctx, retry.WithMaxDuration(&retry.ZeroBackoff{}, time.Minute), func() error {
		addr, err = n.Discover(nodename, fuchsia)
		return err
	}, nil)
	if err != nil {
		return nil, fmt.Errorf("cannot find node %q: %v", nodename, err)
	}
	return addr, nil
}
