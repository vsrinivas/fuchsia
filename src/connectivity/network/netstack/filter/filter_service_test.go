// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package filter

import (
	"context"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/filter"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

func TestGetAndUpdateRules(t *testing.T) {
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocolFactory{
			ipv4.NewProtocol,
		},
		TransportProtocols: []stack.TransportProtocolFactory{
			udp.NewProtocol,
		},
	})
	fi := &filterImpl{filter: New(s)}

	// 0. Prepare test rules.
	trs1 := []filter.Rule{
		{
			Action:       filter.ActionDrop,
			Direction:    filter.DirectionIncoming,
			Proto:        filter.SocketProtocolTcp,
			SrcSubnet:    &net.Subnet{Addr: fidlconv.ToNetIpAddress("\x0a\x00\x00\x00"), PrefixLen: 8},
			SrcPortRange: filter.PortRange{Start: 100, End: 100},
			Log:          true,
		},
	}
	trs2 := []filter.Rule{
		{
			Action:       filter.ActionPass,
			Direction:    filter.DirectionIncoming,
			Proto:        filter.SocketProtocolTcp,
			SrcSubnet:    &net.Subnet{Addr: fidlconv.ToNetIpAddress("\x0b\x00\x00\x00"), PrefixLen: 8},
			SrcPortRange: filter.PortRange{Start: 100, End: 100},
			Log:          true,
		},
	}

	// 1. Get the current rules (should be empty).
	nrs1, generation1, status1, err := fi.GetRules(context.Background())
	if err != nil {
		t.Errorf("GetRules error: %v", err)
	}
	if len(nrs1) != 0 {
		t.Errorf("len(nrs) is not zero: got=%v", nrs1)
	}
	if generation1 != 0 {
		t.Errorf("generation: got=%v, want=%v", generation1, 0)
	}
	if status1 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status1, filter.StatusOk)
	}

	// 2. Update the current rules with trs1.
	status2, err := fi.UpdateRules(context.Background(), trs1, generation1)
	if err != nil {
		t.Errorf("UpdateRules error: %v", err)
	}
	if status2 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status2, filter.StatusOk)
	}

	// 3. Get the current rules (should be trs1).
	nrs3, generation3, status3, err := fi.GetRules(context.Background())
	if err != nil {
		t.Errorf("GetRules error: %v", err)
	}
	if diff := cmp.Diff(nrs3, trs1, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
		t.Errorf("nrs: (-want +got)\n%s", diff)
	}
	if generation3 != generation1+1 {
		t.Errorf("generation: got=%v, want=%v", generation3, generation1+1)
	}
	if status3 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status3, filter.StatusOk)
	}

	// 4. Update the current rules with trs2 using an old generation number.
	status4, err := fi.UpdateRules(context.Background(), trs2, generation1)
	if err != nil {
		t.Errorf("UpdateRules error: %v", err)
	}
	if status4 != filter.StatusErrGenerationMismatch {
		t.Errorf("status: got=%v, want=%v", status4, filter.StatusErrGenerationMismatch)
	}

	// 5. Update the current rules with trs2 using the currenct generation number.
	status5, err := fi.UpdateRules(context.Background(), trs2, generation3)
	if err != nil {
		t.Errorf("UpdateRules error: %v", err)
	}
	if status5 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status5, filter.StatusOk)
	}

	// 6. Get the current rules (should be trs2).
	nrs6, generation6, status6, err := fi.GetRules(context.Background())
	if err != nil {
		t.Errorf("GetRules error: %v", err)
	}
	if diff := cmp.Diff(nrs6, trs2, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
		t.Errorf("nrs: (-want +got)\n%s", diff)
	}
	if generation6 != generation3+1 {
		t.Errorf("generation: got=%v, want=%v", generation6, generation3+1)
	}
	if status6 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status6, filter.StatusOk)
	}
}
