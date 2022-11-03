// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import (
	"os"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/platform"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/parseoutput"
)

func TestGetExpectation(t *testing.T) {
	const (
		SuiteName          string          = "ARP"
		MajorNumber        int             = 1
		MinorNumber        int             = 1
		MissingMinorNumber int             = 2
		ExpectedResult     outcome.Outcome = outcome.Pass
	)
	result, ok := GetExpectation(parseoutput.CaseIdentifier{platform.NS2.String(), SuiteName, MajorNumber, MinorNumber})
	if !ok {
		t.Fatalf("expectation missing for %s %d.%d", SuiteName, MajorNumber, MinorNumber)
	}
	if result != ExpectedResult {
		t.Errorf("wrong expectation for %s %d.%d: got = %s, want = %s", SuiteName, MajorNumber, MinorNumber, result, ExpectedResult)
	}

	os.Setenv("ANVL_DEFAULT_EXPECTATION_PASS", "true")
	result, ok = GetExpectation(parseoutput.CaseIdentifier{platform.NS2.String(), SuiteName, MajorNumber, MissingMinorNumber})
	if !ok {
		t.Fatalf("expectation missing for %s %d.%d", SuiteName, MajorNumber, MissingMinorNumber)
	}
	if result != ExpectedResult {
		t.Errorf("wrong expectation for %s %d.%d: got = %s, want = %s", SuiteName, MajorNumber, MissingMinorNumber, result, ExpectedResult)
	}
}
