// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package netdevice

import (
	"context"
	"fmt"
	"syscall/zx"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"

	"fidl/fuchsia/hardware/network"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ link.Controller = (*MacAddressingClient)(nil)
var _ stack.GSOEndpoint = (*MacAddressingClient)(nil)
var _ stack.LinkEndpoint = (*MacAddressingClient)(nil)

// A fuchsia.hardware.network/Device client with an auxiliary
// fuchsia.hardware.network/MacAddressing proxy that provides the link layer
// information.
type MacAddressingClient struct {
	*Client
	linkAddress tcpip.LinkAddress
	mac         *network.MacAddressingWithCtxInterface
}

func (c *MacAddressingClient) SetPromiscuousMode(b bool) error {
	var mode network.MacFilterMode
	if b {
		mode = network.MacFilterModePromiscuous
	} else {
		// NOTE: Netstack currently is not capable of handling multicast
		// filters, promiscuous mode = false means receive all multicasts still.
		mode = network.MacFilterModeMulticastPromiscuous
	}
	if status, err := c.mac.SetMode(context.Background(), mode); err != nil {
		return err
	} else if zx.Status(status) != zx.ErrOk {
		return &zx.Error{
			Status: zx.Status(status),
			Text:   "fuchsia.hardware.network/MacAddressing.SetMode",
		}
	}

	return nil
}

func (c *MacAddressingClient) LinkAddress() tcpip.LinkAddress {
	return c.linkAddress
}

// NewMacAddressingClient creates a new Network Device client with attached
// link-layer MAC information.
func NewMacAddressingClient(ctx context.Context, dev *network.DeviceWithCtxInterface, mac *network.MacAddressingWithCtxInterface, sessionConfigFactory SessionConfigFactory) (*MacAddressingClient, error) {
	addr, err := mac.GetUnicastAddress(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get mac address: %w", err)
	}
	linkAddress := tcpip.LinkAddress(addr.Octets[:])
	client, err := NewClient(ctx, dev, sessionConfigFactory)
	if err != nil {
		return nil, err
	}

	// Set device to multicast promiscuous to match current behavior. When
	// Netstack controls multicast filters this can be removed.
	if status, err := mac.SetMode(ctx, network.MacFilterModeMulticastPromiscuous); err != nil {
		return nil, fmt.Errorf("failed to set multicast promiscuous: %w", err)
	} else if zx.Status(status) != zx.ErrOk {
		return nil, &zx.Error{
			Status: zx.Status(status),
			Text:   "fuchsia.hardware.network/MacAddressing.SetMode",
		}
	}

	return &MacAddressingClient{
		Client:      client,
		linkAddress: linkAddress,
		mac:         mac,
	}, nil
}
