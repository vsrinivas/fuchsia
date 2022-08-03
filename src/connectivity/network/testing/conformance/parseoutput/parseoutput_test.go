// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parseoutput

import (
	"encoding/json"
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestParseNetworkConformanceCaseEnd(t *testing.T) {
	wantCaseEnd := CaseEnd{
		Identifier: CaseIdentifier{
			Platform:    "NS2",
			SuiteName:   "SOME_Suite-name",
			MajorNumber: 2,
			MinorNumber: 3,
		},
		ExpectedOutcome: "Success",
		ActualOutcome:   "!FAILED!",
		DurationMillis:  1234,
		LogFile:         "/path/to/log/file.txt",
	}
	bytes, err := json.Marshal(wantCaseEnd)
	if err != nil {
		t.Fatal(err)
	}
	line := fmt.Sprintf("[network-conformance case end] %s", string(bytes))
	gotCaseEnd, matched, err := ParseNetworkConformanceCaseEnd(line)
	if err != nil {
		t.Fatal(err)
	}
	if !matched {
		t.Fatalf("ParseNetworkConformanceCaseEnd(%q) unexpectedly returned no match", line)
	}
	if diff := cmp.Diff(wantCaseEnd, gotCaseEnd); diff != "" {
		t.Fatalf("Found mismatch in parsed CaseEnd from %q (-want +got):\n%s", line, diff)
	}
}

func TestCaseIdentifierSerializationRoundtrip(t *testing.T) {
	wantIdent := CaseIdentifier{
		Platform:    "NS2",
		SuiteName:   "SOME_Suite-name",
		MajorNumber: 2,
		MinorNumber: 3,
	}
	s := wantIdent.String()

	got, err := ParseCaseIdentifier(s)
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(wantIdent, got); diff != "" {
		t.Fatalf(
			"Found mismatch in parsed CaseIdentifier from %q (-want +got):\n%s",
			s,
			diff,
		)
	}
}
