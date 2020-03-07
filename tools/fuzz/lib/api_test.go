// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestParseArgsErrors(t *testing.T) {
	// Check invalid commands and too few/many args
	testCases := map[string]bool{
		"":                                   false,
		"invalid":                            false,
		"version":                            true,
		"version extra":                      false,
		"stop_instance":                      false,
		"stop_instance -handle handle":       true,
		"stop_instance -handle handle extra": false,
	}

	for cmdline, shouldBeValid := range testCases {
		_, err := ParseArgs(strings.Split(cmdline, " "))
		if (err == nil) != shouldBeValid {
			t.Fatalf("unexpected result parsing %q: %s", cmdline, err)
		}
	}
}

func TestParseArgs(t *testing.T) {
	// Check that an example APICommand is constructed as expected
	args := []string{"run_fuzzer", "-handle", "handle", "-fuzzer", "fuzzer",
		"--", "extra1", "extra2"}

	got, err := ParseArgs(args)
	if err != nil {
		t.Fatalf("unexpected parsing failure: %s", err)
	}

	want := &APICommand{
		name:      RunFuzzer,
		handle:    "handle",
		fuzzer:    "fuzzer",
		extraArgs: []string{"extra1", "extra2"},
	}
	if diff := cmp.Diff(want, got, cmp.AllowUnexported(APICommand{})); diff != "" {
		t.Fatalf("ParseArgs mismatch (-want +got):\n%s", diff)
	}
}
