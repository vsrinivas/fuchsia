// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package outputs_test

import (
	"bytes"
	"strings"
	"time"
	"testing"

	"fuchsia.googlesource.com/tools/cmd/testrunner/outputs"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testrunner"
)

func TestTapOutput(t *testing.T) {
	s := time.Unix(0, 0)
	inputs := []testrunner.TestResult{{
		Name:   "test_a",
		Result: runtests.TestSuccess,
		StartTime: s,
		EndTime: s.Add(time.Second * 2),
	}, {
		Name:   "test_b",
		Result: runtests.TestFailure,
		StartTime: s.Add(time.Minute * 1),
		EndTime: s.Add(time.Minute * 2),
	}}

	var buf bytes.Buffer
	output := outputs.NewTAPOutput(&buf, 10)
	for _, input := range inputs {
		output.Record(input)
	}

	expectedOutput := strings.TrimSpace(`
TAP version 13
1..10
ok 1 test_a (2s)
not ok 2 test_b (1m0s)
`)

	actualOutput := strings.TrimSpace(buf.String())
	if actualOutput != expectedOutput {
		t.Errorf("got\n%q\nbut wanted\n%q\n", actualOutput, expectedOutput)
	}
}
