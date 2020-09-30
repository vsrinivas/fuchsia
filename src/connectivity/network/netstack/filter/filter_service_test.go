// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"context"
	"testing"

	"fidl/fuchsia/net/filter"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
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
	fi := &filterImpl{filter: New(s.PortManager)}

	// 0. Prepare test rules.
	srcSubnet1, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		t.Fatalf("NewSubnet error: %v", err)
	}
	trs1, err := fromRules([]Rule{
		{
			action:       Drop,
			direction:    Incoming,
			transProto:   header.TCPProtocolNumber,
			srcSubnet:    &srcSubnet1,
			srcPortRange: PortRange{100, 100},
			log:          true,
		},
	})
	if err != nil {
		t.Fatalf("fromRules error: %v", err)
	}
	srcSubnet2, err := tcpip.NewSubnet("\x0b\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		t.Fatalf("NewSubnet error: %v", err)
	}
	trs2, err := fromRules([]Rule{
		{
			action:       Pass,
			direction:    Incoming,
			transProto:   header.UDPProtocolNumber,
			srcSubnet:    &srcSubnet2,
			srcPortRange: PortRange{100, 100},
			log:          true,
		},
	})
	if err != nil {
		t.Fatalf("fromRules error: %v", err)
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

func TestGetAndUpdateNatRules(t *testing.T) {
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocolFactory{
			ipv4.NewProtocol,
		},
		TransportProtocols: []stack.TransportProtocolFactory{
			udp.NewProtocol,
		},
	})
	fi := &filterImpl{filter: New(s.PortManager)}

	// 0. Prepare test rules.
	srcSubnet1, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		t.Fatalf("NewSubnet error: %v", err)
	}
	trs1, err := fromNATs([]NAT{
		{
			transProto: header.UDPProtocolNumber,
			srcSubnet:  &srcSubnet1,
			newSrcAddr: testRouterNICAddr2,
		},
	})
	if err != nil {
		t.Fatalf("fromNATs error: %v", err)
	}
	srcSubnet2, err := tcpip.NewSubnet("\x0b\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		t.Fatalf("NewSubnet error: %v", err)
	}
	trs2, err := fromNATs([]NAT{
		{
			transProto: header.TCPProtocolNumber,
			srcSubnet:  &srcSubnet2,
			newSrcAddr: testRouterNICAddr2,
		},
	})
	if err != nil {
		t.Fatalf("fromNATs error: %v", err)
	}

	// 1. Get the current rules (should be empty).
	nrs1, generation1, status1, err := fi.GetNatRules(context.Background())
	if err != nil {
		t.Errorf("GetNatRules error: %v", err)
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
	status2, err := fi.UpdateNatRules(context.Background(), trs1, generation1)
	if err != nil {
		t.Errorf("UpdateNatRules error: %v", err)
	}
	if status2 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status2, filter.StatusOk)
	}

	// 3. Get the current rules (should be trs1).
	nrs3, generation3, status3, err := fi.GetNatRules(context.Background())
	if err != nil {
		t.Errorf("GetNatRules error: %v", err)
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
	status4, err := fi.UpdateNatRules(context.Background(), trs2, generation1)
	if err != nil {
		t.Errorf("UpdateNatRules error: %v", err)
	}
	if status4 != filter.StatusErrGenerationMismatch {
		t.Errorf("status: got=%v, want=%v", status4, filter.StatusErrGenerationMismatch)
	}

	// 5. Update the current rules with trs2 using the currenct generation number.
	status5, err := fi.UpdateNatRules(context.Background(), trs2, generation3)
	if err != nil {
		t.Errorf("UpdateNatRules error: %v", err)
	}
	if status5 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status5, filter.StatusOk)
	}

	// 6. Get the current rules (should be trs2).
	nrs6, generation6, status6, err := fi.GetNatRules(context.Background())
	if err != nil {
		t.Errorf("GetNatRules error: %v", err)
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

func TestGetAndUpdateRdrRules(t *testing.T) {
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocolFactory{
			ipv4.NewProtocol,
		},
		TransportProtocols: []stack.TransportProtocolFactory{
			udp.NewProtocol,
		},
	})
	fi := &filterImpl{filter: New(s.PortManager)}

	// 0. Prepare test rules.
	trs1, err := fromRDRs([]RDR{
		{
			transProto:      header.UDPProtocolNumber,
			dstAddr:         testRouterNICAddr2,
			dstPortRange:    PortRange{testRouterPort, testRouterPort},
			newDstAddr:      testLANNICAddr,
			newDstPortRange: PortRange{testLANPort, testLANPort},
		},
	})
	if err != nil {
		t.Fatalf("fromRDRs error: %v", err)
	}
	trs2, err := fromRDRs([]RDR{
		{
			transProto:      header.TCPProtocolNumber,
			dstAddr:         testRouterNICAddr2,
			dstPortRange:    PortRange{testRouterPort, testRouterPort},
			newDstAddr:      testLANNICAddr,
			newDstPortRange: PortRange{testLANPort, testLANPort},
		},
	})
	if err != nil {
		t.Fatalf("fromRDRs error: %v", err)
	}

	// 1. Get the current rules (should be empty).
	nrs1, generation1, status1, err := fi.GetRdrRules(context.Background())
	if err != nil {
		t.Errorf("GetRdrRules error: %v", err)
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
	status2, err := fi.UpdateRdrRules(context.Background(), trs1, generation1)
	if err != nil {
		t.Errorf("UpdateRdrRules error: %v", err)
	}
	if status2 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status2, filter.StatusOk)
	}

	// 3. Get the current rules (should be trs1).
	nrs3, generation3, status3, err := fi.GetRdrRules(context.Background())
	if err != nil {
		t.Errorf("GetRdrRules error: %v", err)
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
	status4, err := fi.UpdateRdrRules(context.Background(), trs2, generation1)
	if err != nil {
		t.Errorf("UpdateRdrRules error: %v", err)
	}
	if status4 != filter.StatusErrGenerationMismatch {
		t.Errorf("status: got=%v, want=%v", status4, filter.StatusErrGenerationMismatch)
	}

	// 5. Update the current rules with trs2 using the currenct generation number.
	status5, err := fi.UpdateRdrRules(context.Background(), trs2, generation3)
	if err != nil {
		t.Errorf("UpdateRdrRules error: %v", err)
	}
	if status5 != filter.StatusOk {
		t.Errorf("status: got=%v, want=%v", status5, filter.StatusOk)
	}

	// 6. Get the current rules (should be trs2).
	nrs6, generation6, status6, err := fi.GetRdrRules(context.Background())
	if err != nil {
		t.Errorf("GetRdrRules error: %v", err)
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
