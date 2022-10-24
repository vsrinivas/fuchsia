// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import (
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/platform"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/parseoutput"
)

// TODO(https://fxbug.dev/92179): Test expectations are intended to potentially be moved into a
// config file (perhaps JSON) rather than being embedded in Go in this way.
var expectations map[parseoutput.CaseIdentifier]outcome.Outcome = func() map[parseoutput.CaseIdentifier]outcome.Outcome {
	m := make(map[parseoutput.CaseIdentifier]outcome.Outcome)

	addAllExpectations := func(suite string, expects map[AnvlCaseNumber]outcome.Outcome) {
		for k, v := range expects {
			ident := parseoutput.CaseIdentifier{
				SuiteName:   strings.ToUpper(suite),
				Platform:    platform.NS2.String(),
				MajorNumber: k.MajorNumber,
				MinorNumber: k.MinorNumber,
			}
			m[ident] = v
		}
	}

	// keep-sorted start
	addAllExpectations("arp", arpExpectations)
	addAllExpectations("dhcp-client", dhcpClientExpectations)
	addAllExpectations("dhcp-server", dhcpServerExpectations)
	addAllExpectations("dhcpv6-client", dhcpv6ClientExpectations)
	addAllExpectations("icmp", icmpExpectations)
	addAllExpectations("icmpv6", icmpv6Expectations)
	addAllExpectations("icmpv6-router", icmpv6RouterExpectations)
	addAllExpectations("igmp", igmpExpectations)
	addAllExpectations("ip", ipExpectations)
	addAllExpectations("ipv6", ipv6Expectations)
	addAllExpectations("ipv6-mld", ipv6MldExpectations)
	addAllExpectations("ipv6-ndp", ipv6ndpExpectations)
	addAllExpectations("ipv6-pmtu", ipv6PmtuExpectations)
	addAllExpectations("ipv6-router", ipv6RouterExpectations)
	addAllExpectations("tcp-advanced", tcpAdvancedExpectations)
	addAllExpectations("tcp-advanced-v6", tcpAdvancedV6Expectations)
	addAllExpectations("tcp-core", tcpCoreExpectations)
	addAllExpectations("tcp-core-v6", tcpcorev6Expectations)
	addAllExpectations("tcp-highperf", tcpHighperfExpectations)
	addAllExpectations("tcp-highperf-v6", tcpHighperfV6Expectations)
	addAllExpectations("udp", udpExpectations)
	addAllExpectations("udp-v6", udpV6Expectations)
	// keep-sorted end

	return m
}()

type AnvlCaseNumber struct {
	MajorNumber int
	MinorNumber int
}

var Pass = outcome.Pass
var Fail = outcome.Fail
var Inconclusive = outcome.Inconclusive
var Flaky = outcome.Flaky

func GetExpectation(
	ident parseoutput.CaseIdentifier,
) (outcome.Outcome, bool) {
	expectation, ok := expectations[ident]
	if !ok && os.Getenv("ANVL_DEFAULT_EXPECTATION_PASS") != "" {
		return outcome.Pass, true
	}
	return expectation, ok
}
