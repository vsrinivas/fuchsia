// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

// Package filter provides the implementation of packet filter.
package filter

import (
	"fmt"
	"sync"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net/filter"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const tag = "filter"

type Filter struct {
	stack          *stack.Stack
	defaultV4Table stack.Table
	defaultV6Table stack.Table

	filterDisabledNICMatcher filterDisabledNICMatcher

	mu struct {
		sync.RWMutex
		rules      []filter.Rule
		v4Table    stack.Table
		v6Table    stack.Table
		generation uint32
	}
}

func New(s *stack.Stack) *Filter {
	defaultTables := stack.DefaultTables(s.Seed())
	defaultV4Table := defaultTables.GetTable(stack.FilterID, false /* ipv6 */)
	defaultV6Table := defaultTables.GetTable(stack.FilterID, true /* ipv6 */)

	f := &Filter{
		stack:          s,
		defaultV4Table: defaultV4Table,
		defaultV6Table: defaultV6Table,
	}
	f.filterDisabledNICMatcher.init()
	f.mu.Lock()
	f.mu.v4Table = defaultV4Table
	f.mu.v6Table = defaultV6Table
	f.mu.Unlock()
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
	f.filterDisabledNICMatcher.mu.Lock()
	defer f.filterDisabledNICMatcher.mu.Unlock()
	for k, v := range f.filterDisabledNICMatcher.mu.nicNames {
		if v == id {
			delete(f.filterDisabledNICMatcher.mu.nicNames, k)
			break
		}
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
	f.mu.RLock()
	g := f.mu.generation
	f.mu.RUnlock()

	if generation != g {
		return filter.FilterUpdateRulesResultWithErr(filter.FilterUpdateRulesErrorGenerationMismatch)
	}

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
	if err := iptables.ReplaceTable(stack.FilterID, v4Table, false /* ipv6 */); err != nil {
		_ = syslog.ErrorTf(tag, "error replacing iptables = %s", err)
		return filter.FilterUpdateRulesResultWithErr(filter.FilterUpdateRulesErrorInternal)
	}
	if err := iptables.ReplaceTable(stack.FilterID, v6Table, true /* ipv6 */); err != nil {
		_ = syslog.ErrorTf(tag, "error replacing ip6tables = %s", err)
		return filter.FilterUpdateRulesResultWithErr(filter.FilterUpdateRulesErrorInternal)
	}
	f.mu.generation++
	return filter.FilterUpdateRulesResultWithResponse(filter.FilterUpdateRulesResponse{})
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
