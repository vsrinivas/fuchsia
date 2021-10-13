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

	validateGetRulesResult := func(t *testing.T, result filter.FilterGetRulesResult, rules []filter.Rule) {
		t.Helper()
		switch result.Which() {
		case filter.FilterGetRulesResultResponse:
			if diff := cmp.Diff(result.Response.Rules, rules, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
				t.Errorf("result.Response.Rules: (-want +got)\n%s", diff)
			}
		case filter.FilterGetRulesResultErr:
			t.Errorf("result.Err = %s", result.Err)
		}
	}
	validateUpdateRulesResult := func(t *testing.T, result filter.FilterUpdateRulesResult) {
		t.Helper()
		switch result.Which() {
		case filter.FilterUpdateRulesResultResponse:
			if got, want := result, filter.FilterUpdateRulesResultWithResponse(filter.FilterUpdateRulesResponse{}); got != want {
				t.Errorf("got result = %#v, want = %#v", got, want)
			}
		case filter.FilterUpdateRulesResultErr:
			t.Errorf("result.Err = %s", result.Err)
		}
	}
	validateUpdateRulesResultErr := func(t *testing.T, result filter.FilterUpdateRulesResult, err filter.FilterUpdateRulesError) {
		t.Helper()
		switch result.Which() {
		case filter.FilterUpdateRulesResultResponse:
			t.Errorf("result.Response = %#v", result.Response)
		case filter.FilterUpdateRulesResultErr:
			if got, want := result.Err, err; got != want {
				t.Errorf("got result.Err = %s, want = %s", result.Err, want)
			}
		}
	}

	var lastGeneration uint32

	// Get the current rules (should be empty).
	{
		result, err := fi.GetRules(context.Background())
		if err != nil {
			t.Errorf("GetRules error: %s", err)
		}
		validateGetRulesResult(t, result, nil)
		lastGeneration = result.Response.Generation
	}
	// Update the current rules with trs1.
	{
		result, err := fi.UpdateRules(context.Background(), trs1, lastGeneration)
		if err != nil {
			t.Errorf("UpdateRules error: %s", err)
		}
		validateUpdateRulesResult(t, result)
	}
	// Get the current rules (should be trs1).
	{
		result, err := fi.GetRules(context.Background())
		if err != nil {
			t.Errorf("GetRules error: %s", err)
		}
		validateGetRulesResult(t, result, trs1)
		if got, notWant := result.Response.Generation, lastGeneration; got == notWant {
			t.Errorf("got result.Response.Generation = %d (want = not %d)", got, notWant)
		}
		lastGeneration = result.Response.Generation
	}
	// Try to update the current rules with trs2 using a wrong generation number (should fail).
	{
		result, err := fi.UpdateRules(context.Background(), trs2, lastGeneration-1)
		if err != nil {
			t.Errorf("UpdateRules error: %s", err)
		}
		validateUpdateRulesResultErr(t, result, filter.FilterUpdateRulesErrorGenerationMismatch)
	}
	// Update the current rules with trs2 using a correct generation number.
	{
		result, err := fi.UpdateRules(context.Background(), trs2, lastGeneration)
		if err != nil {
			t.Errorf("UpdateRules error: %s", err)
		}
		validateUpdateRulesResult(t, result)
	}
	// Get the current rules (should be trs2).
	{
		result, err := fi.GetRules(context.Background())
		if err != nil {
			t.Errorf("GetRules error: %s", err)
		}
		validateGetRulesResult(t, result, trs2)
		if got, notWant := result.Response.Generation, lastGeneration; got == notWant {
			t.Errorf("got result.Response.Generation = %d (want = not %d)", got, notWant)
		}
	}
}
