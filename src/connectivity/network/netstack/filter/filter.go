// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

// Package filter provides the implementation of packet filter.
package filter

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"

	"fidl/fuchsia/net/filter"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const tag = "filter"

type Filter struct {
	stack             *stack.Stack
	defaultV4Table    stack.Table
	defaultV6Table    stack.Table
	defaultV4NATTable stack.Table
	defaultV6NATTable stack.Table

	filterDisabledNICMatcher filterDisabledNICMatcher

	mu struct {
		sync.RWMutex

		rules      []filter.Rule
		v4Table    stack.Table
		v6Table    stack.Table
		generation uint32

		natRules      []filter.Nat
		v4NATTable    stack.Table
		v6NATTable    stack.Table
		natGeneration uint32
	}
}

func New(s *stack.Stack) *Filter {
	defaultTables := stack.DefaultTables(s.Clock(), s.Rand())
	defaultV4Table := defaultTables.GetTable(stack.FilterID, false /* ipv6 */)
	defaultV6Table := defaultTables.GetTable(stack.FilterID, true /* ipv6 */)
	defaultV4NATTable := defaultTables.GetTable(stack.NATID, false /* ipv6 */)
	defaultV6NATTable := defaultTables.GetTable(stack.NATID, true /* ipv6 */)

	f := &Filter{
		stack:             s,
		defaultV4Table:    defaultV4Table,
		defaultV6Table:    defaultV6Table,
		defaultV4NATTable: defaultV4NATTable,
		defaultV6NATTable: defaultV6NATTable,
	}
	f.filterDisabledNICMatcher.init()
	f.mu.Lock()
	defer f.mu.Unlock()
	f.mu.v4Table = defaultV4Table
	f.mu.v6Table = defaultV6Table
	f.mu.v4NATTable = defaultV4NATTable
	f.mu.v6NATTable = defaultV6NATTable
	return f
}

func (f *Filter) enableInterface(id tcpip.NICID) filter.FilterEnableInterfaceResult {
	name := f.stack.FindNICNameFromID(id)
	if name == "" {
		return filter.FilterEnableInterfaceResultWithErr(filter.FilterEnableInterfaceErrorNotFound)
	}
	f.filterDisabledNICMatcher.mu.Lock()
	defer f.filterDisabledNICMatcher.mu.Unlock()
	f.filterDisabledNICMatcher.mu.nicNames[name] = id
	return filter.FilterEnableInterfaceResultWithResponse(filter.FilterEnableInterfaceResponse{})
}

func (f *Filter) disableInterface(id tcpip.NICID) filter.FilterDisableInterfaceResult {
	name := f.stack.FindNICNameFromID(id)
	if name == "" {
		return filter.FilterDisableInterfaceResultWithErr(filter.FilterDisableInterfaceErrorNotFound)
	}
	f.filterDisabledNICMatcher.mu.Lock()
	defer f.filterDisabledNICMatcher.mu.Unlock()
	delete(f.filterDisabledNICMatcher.mu.nicNames, name)
	return filter.FilterDisableInterfaceResultWithResponse(filter.FilterDisableInterfaceResponse{})
}

func (f *Filter) RemovedNIC(id tcpip.NICID) {
	f.mu.Lock()
	defer f.mu.Unlock()

	func() {
		f.filterDisabledNICMatcher.mu.Lock()
		defer f.filterDisabledNICMatcher.mu.Unlock()
		for k, v := range f.filterDisabledNICMatcher.mu.nicNames {
			if v == id {
				delete(f.filterDisabledNICMatcher.mu.nicNames, k)
				break
			}
		}
	}()

	requiresNATUpdate := false
	newNATRules := f.mu.natRules[:0]
	for _, r := range f.mu.natRules {
		if nicID := tcpip.NICID(r.OutgoingNic); nicID == id {
			requiresNATUpdate = true
		} else {
			newNATRules = append(newNATRules, r)
		}
	}

	if requiresNATUpdate {
		newV4NATTable, newV6NATTable, ok := f.parseNATRules(newNATRules)
		if !ok {
			panic(fmt.Sprintf("newNATRules should be valid since it is a subset of previously parsed rules; newNATRules = %#v", newNATRules))
		}

		f.updateNATRulesLocked(newNATRules, newV4NATTable, newV6NATTable)
	}
}

func (f *Filter) getRules() filter.FilterGetRulesResult {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return filter.FilterGetRulesResultWithResponse(filter.FilterGetRulesResponse{
		Rules:      f.mu.rules,
		Generation: f.mu.generation,
	})
}

func (f *Filter) updateRules(rules []filter.Rule, generation uint32) filter.FilterUpdateRulesResult {
	v4Table, v6Table, ok := f.parseRules(rules)
	if !ok {
		return filter.FilterUpdateRulesResultWithErr(filter.FilterUpdateRulesErrorBadRule)
	}

	f.mu.Lock()
	defer f.mu.Unlock()
	if f.mu.generation != generation {
		return filter.FilterUpdateRulesResultWithErr(filter.FilterUpdateRulesErrorGenerationMismatch)
	}

	f.mu.rules = rules
	f.mu.v4Table = v4Table
	f.mu.v6Table = v6Table

	iptables := f.stack.IPTables()
	iptables.ReplaceTable(stack.FilterID, v4Table, false /* ipv6 */)
	iptables.ReplaceTable(stack.FilterID, v6Table, true /* ipv6 */)
	f.mu.generation++
	return filter.FilterUpdateRulesResultWithResponse(filter.FilterUpdateRulesResponse{})
}

func (f *Filter) getNATRules() filter.FilterGetNatRulesResult {
	f.mu.RLock()
	defer f.mu.RUnlock()
	return filter.FilterGetNatRulesResultWithResponse(filter.FilterGetNatRulesResponse{
		Rules:      f.mu.natRules,
		Generation: f.mu.natGeneration,
	})
}

func (f *Filter) updateNATRules(rules []filter.Nat, generation uint32) filter.FilterUpdateNatRulesResult {
	v4Table, v6Table, ok := f.parseNATRules(rules)
	if !ok {
		return filter.FilterUpdateNatRulesResultWithErr(filter.FilterUpdateNatRulesErrorBadRule)
	}

	f.mu.Lock()
	defer f.mu.Unlock()
	if f.mu.natGeneration != generation {
		return filter.FilterUpdateNatRulesResultWithErr(filter.FilterUpdateNatRulesErrorGenerationMismatch)
	}

	f.updateNATRulesLocked(rules, v4Table, v6Table)

	return filter.FilterUpdateNatRulesResultWithResponse(filter.FilterUpdateNatRulesResponse{})
}

func (f *Filter) updateNATRulesLocked(rules []filter.Nat, v4Table, v6Table stack.Table) {
	f.mu.natRules = rules
	f.mu.v4NATTable = v4Table
	f.mu.v6NATTable = v6Table

	iptables := f.stack.IPTables()
	iptables.ReplaceTable(stack.NATID, v4Table, false /* ipv6 */)
	iptables.ReplaceTable(stack.NATID, v6Table, true /* ipv6 */)
	f.mu.natGeneration++
}

func isPortRangeAny(p filter.PortRange) bool {
	return p.Start == 0 && p.End == 0
}

func isPortRangeValid(p filter.PortRange) bool {
	return isPortRangeAny(p) || (1 <= p.Start && p.Start <= p.End)
}

func (f *Filter) parseRules(rules []filter.Rule) (v4Table stack.Table, v6Table stack.Table, ok bool) {
	type ipType int
	const (
		generic ipType = iota
		ipv4Only
		ipv6Only
	)

	if len(rules) == 0 {
		return f.defaultV4Table, f.defaultV6Table, true
	}

	allowPacketsForFilterDisbledNICs := stack.Rule{
		Matchers: []stack.Matcher{&f.filterDisabledNICMatcher},
		Target:   &stack.AcceptTarget{},
	}
	v4InputRules := []stack.Rule{allowPacketsForFilterDisbledNICs}
	v4OutputRules := []stack.Rule{allowPacketsForFilterDisbledNICs}
	v6InputRules := []stack.Rule{allowPacketsForFilterDisbledNICs}
	v6OutputRules := []stack.Rule{allowPacketsForFilterDisbledNICs}

	for _, r := range rules {
		var ipTypeValue ipType
		var ipHdrFilter stack.IPHeaderFilter

		if src := r.SrcSubnet; src != nil {
			ipHdrFilter.SrcInvert = r.SrcSubnetInvertMatch
			subnet := fidlconv.ToTCPIPSubnet(*src)
			ipHdrFilter.Src = subnet.ID()
			ipHdrFilter.SrcMask = tcpip.Address(subnet.Mask())

			switch l := len(ipHdrFilter.Src); l {
			case header.IPv4AddressSize:
				ipTypeValue = ipv4Only
			case header.IPv6AddressSize:
				ipTypeValue = ipv6Only
			default:
				return stack.Table{}, stack.Table{}, false
			}
		}

		if dst := r.DstSubnet; dst != nil {
			ipHdrFilter.DstInvert = r.DstSubnetInvertMatch
			subnet := fidlconv.ToTCPIPSubnet(*dst)
			ipHdrFilter.Dst = subnet.ID()
			ipHdrFilter.DstMask = tcpip.Address(subnet.Mask())

			var dstIpType ipType
			switch l := len(ipHdrFilter.Dst); l {
			case header.IPv4AddressSize:
				dstIpType = ipv4Only
			case header.IPv6AddressSize:
				dstIpType = ipv6Only
			default:
				return stack.Table{}, stack.Table{}, false
			}

			if ipTypeValue == generic {
				ipTypeValue = dstIpType
			} else if ipTypeValue != dstIpType {
				return stack.Table{}, stack.Table{}, false
			}
		}

		var matchers []stack.Matcher
		switch r.Proto {
		case filter.SocketProtocolAny:
		case filter.SocketProtocolIcmp:
			if ipTypeValue == ipv6Only {
				return stack.Table{}, stack.Table{}, false
			}
			ipTypeValue = ipv4Only

			ipHdrFilter.CheckProtocol = true
			ipHdrFilter.Protocol = header.ICMPv4ProtocolNumber
		case filter.SocketProtocolTcp:
			ipHdrFilter.CheckProtocol = true
			ipHdrFilter.Protocol = header.TCPProtocolNumber

			if !isPortRangeAny(r.SrcPortRange) {
				if !isPortRangeValid(r.SrcPortRange) {
					return stack.Table{}, stack.Table{}, false
				}
				matchers = append(matchers, NewTCPSourcePortMatcher(r.SrcPortRange.Start, r.SrcPortRange.End))
			}

			if !isPortRangeAny(r.DstPortRange) {
				if !isPortRangeValid(r.DstPortRange) {
					return stack.Table{}, stack.Table{}, false
				}
				matchers = append(matchers, NewTCPDestinationPortMatcher(r.DstPortRange.Start, r.DstPortRange.End))
			}

		case filter.SocketProtocolUdp:
			ipHdrFilter.CheckProtocol = true
			ipHdrFilter.Protocol = header.UDPProtocolNumber

			if !isPortRangeAny(r.SrcPortRange) {
				if !isPortRangeValid(r.SrcPortRange) {
					return stack.Table{}, stack.Table{}, false
				}
				matchers = append(matchers, NewUDPSourcePortMatcher(r.SrcPortRange.Start, r.SrcPortRange.End))
			}

			if !isPortRangeAny(r.DstPortRange) {
				if !isPortRangeValid(r.DstPortRange) {
					return stack.Table{}, stack.Table{}, false
				}
				matchers = append(matchers, NewUDPDestinationPortMatcher(r.DstPortRange.Start, r.DstPortRange.End))
			}
		case filter.SocketProtocolIcmpv6:
			if ipTypeValue == ipv4Only {
				return stack.Table{}, stack.Table{}, false
			}
			ipTypeValue = ipv6Only

			ipHdrFilter.CheckProtocol = true
			ipHdrFilter.Protocol = header.ICMPv6ProtocolNumber
		default:
			return stack.Table{}, stack.Table{}, false
		}

		var target stack.Target
		switch r.Action {
		case filter.ActionPass:
			target = &stack.AcceptTarget{}
			if r.KeepState {
				// TODO(https://fxbug.dev/68501): Support keep state.
				return stack.Table{}, stack.Table{}, false
			}
		case filter.ActionDrop:
			target = &stack.DropTarget{}
		default:
			return stack.Table{}, stack.Table{}, false
		}

		rule := stack.Rule{
			Filter:   ipHdrFilter,
			Matchers: matchers,
			Target:   target,
		}

		switch r.Direction {
		case filter.DirectionIncoming:
			switch ipTypeValue {
			case generic:
				v4InputRules = append(v4InputRules, rule)
				v6InputRules = append(v6InputRules, rule)
			case ipv4Only:
				v4InputRules = append(v4InputRules, rule)
			case ipv6Only:
				v6InputRules = append(v6InputRules, rule)
			default:
				panic(fmt.Sprintf("unrecognied ipTypeValue = %d", ipTypeValue))
			}
		case filter.DirectionOutgoing:
			switch ipTypeValue {
			case generic:
				v4OutputRules = append(v4OutputRules, rule)
				v6OutputRules = append(v6OutputRules, rule)
			case ipv4Only:
				v4OutputRules = append(v4OutputRules, rule)
			case ipv6Only:
				v6OutputRules = append(v6OutputRules, rule)
			default:
				panic(fmt.Sprintf("unrecognied ipTypeValue = %d", ipTypeValue))
			}
		default:
			return stack.Table{}, stack.Table{}, false
		}
	}

	v4InputRules = append(v4InputRules, stack.Rule{Target: &stack.AcceptTarget{}})
	v4OutputRules = append(v4OutputRules, stack.Rule{Target: &stack.AcceptTarget{}})
	v6InputRules = append(v6InputRules, stack.Rule{Target: &stack.AcceptTarget{}})
	v6OutputRules = append(v6OutputRules, stack.Rule{Target: &stack.AcceptTarget{}})

	v4Table = stack.Table{
		Rules: append(append([]stack.Rule{{Target: &stack.AcceptTarget{}}}, v4InputRules...), v4OutputRules...),
		BuiltinChains: [stack.NumHooks]int{
			stack.Prerouting:  0,
			stack.Input:       1,
			stack.Forward:     0,
			stack.Output:      1 + len(v4InputRules),
			stack.Postrouting: 0,
		},
	}
	v6Table = stack.Table{
		Rules: append(append([]stack.Rule{{Target: &stack.AcceptTarget{}}}, v6InputRules...), v6OutputRules...),
		BuiltinChains: [stack.NumHooks]int{
			stack.Prerouting:  0,
			stack.Input:       1,
			stack.Forward:     0,
			stack.Output:      1 + len(v6InputRules),
			stack.Postrouting: 0,
		},
	}
	return v4Table, v6Table, true
}

func (f *Filter) parseNATRules(natRules []filter.Nat) (v4Table stack.Table, v6Table stack.Table, ok bool) {
	type ipType int
	const (
		_ ipType = iota
		ipv4Only
		ipv6Only
	)

	if len(natRules) == 0 {
		return f.defaultV4NATTable, f.defaultV6NATTable, true
	}

	allowPacketsForFilterDisbledNICs := stack.Rule{
		Matchers: []stack.Matcher{&f.filterDisabledNICMatcher},
		Target:   &stack.AcceptTarget{},
	}
	v4PostroutingRules := []stack.Rule{allowPacketsForFilterDisbledNICs}
	v6PostroutingRules := []stack.Rule{allowPacketsForFilterDisbledNICs}

	for _, r := range natRules {
		var ipTypeValue ipType
		var ipHdrFilter stack.IPHeaderFilter
		var netProto tcpip.NetworkProtocolNumber

		{
			subnet := fidlconv.ToTCPIPSubnet(r.SrcSubnet)
			ipHdrFilter.Src = subnet.ID()
			ipHdrFilter.SrcMask = tcpip.Address(subnet.Mask())

			switch l := len(ipHdrFilter.Src); l {
			case header.IPv4AddressSize:
				ipTypeValue = ipv4Only
				netProto = header.IPv4ProtocolNumber
			case header.IPv6AddressSize:
				ipTypeValue = ipv6Only
				netProto = header.IPv6ProtocolNumber
			default:
				panic(fmt.Sprintf("unexpected address length = %d; address = %#v", l, ipHdrFilter.Src))
			}
		}

		var matchers []stack.Matcher
		ipHdrFilter.CheckProtocol = true
		switch r.Proto {
		case filter.SocketProtocolAny:
			ipHdrFilter.CheckProtocol = false
		case filter.SocketProtocolIcmp:
			if ipTypeValue != ipv4Only {
				return stack.Table{}, stack.Table{}, false
			}

			ipHdrFilter.Protocol = header.ICMPv4ProtocolNumber
		case filter.SocketProtocolIcmpv6:
			if ipTypeValue != ipv6Only {
				return stack.Table{}, stack.Table{}, false
			}

			ipHdrFilter.Protocol = header.ICMPv6ProtocolNumber
		case filter.SocketProtocolTcp:
			ipHdrFilter.Protocol = header.TCPProtocolNumber
		case filter.SocketProtocolUdp:
			ipHdrFilter.Protocol = header.UDPProtocolNumber
		default:
			return stack.Table{}, stack.Table{}, false
		}

		if nicID := tcpip.NICID(r.OutgoingNic); nicID != 0 {
			if name := f.stack.FindNICNameFromID(nicID); len(name) != 0 {
				ipHdrFilter.OutputInterface = name
			} else {
				// NIC not found.
				return stack.Table{}, stack.Table{}, false
			}
		}

		rule := stack.Rule{
			Filter:   ipHdrFilter,
			Matchers: matchers,
			Target:   &stack.MasqueradeTarget{NetworkProtocol: netProto},
		}

		switch ipTypeValue {
		case ipv4Only:
			v4PostroutingRules = append(v4PostroutingRules, rule)
		case ipv6Only:
			v6PostroutingRules = append(v6PostroutingRules, rule)
		default:
			panic(fmt.Sprintf("unhandled ipTypeValue = %d", ipTypeValue))
		}
	}

	v4PostroutingRules = append(v4PostroutingRules, stack.Rule{Target: &stack.AcceptTarget{}})
	v6PostroutingRules = append(v6PostroutingRules, stack.Rule{Target: &stack.AcceptTarget{}})

	v4Table = stack.Table{
		Rules: append([]stack.Rule{{Target: &stack.AcceptTarget{}}}, v4PostroutingRules...),
		BuiltinChains: [stack.NumHooks]int{
			stack.Postrouting: 1,
		},
	}
	v6Table = stack.Table{
		Rules: append([]stack.Rule{{Target: &stack.AcceptTarget{}}}, v6PostroutingRules...),
		BuiltinChains: [stack.NumHooks]int{
			stack.Postrouting: 1,
		},
	}
	return v4Table, v6Table, true
}
