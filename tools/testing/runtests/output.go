// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runtests

import (
	"context"
	"fmt"
	"io"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
)

const (
	SuccessSignature = "[runtests][PASSED]"
	FailureSignature = "[runtests][FAILED]"
)

// TestPassed reads in the output from a runtests invocation of a single test and returns whether
// that test succeeded. The expected signature, eg "[runtests][PASSED] /test/name", must match the
// one in
// https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/zircon/system/ulib/runtests-utils/fuchsia-run-test.cc
func TestPassed(ctx context.Context, testOutput io.Reader, name string) (bool, error) {
	resultSignature := fmt.Sprintf("(%s|%s) %s", regexp.QuoteMeta(SuccessSignature), regexp.QuoteMeta(FailureSignature), regexp.QuoteMeta(name))
	resultSignatureRE, err := regexp.Compile(resultSignature)
	if err != nil {
		return false, fmt.Errorf("unable to compile regular expression for test name %v: %w", name, err)
	}
	resultMatchLength := len(SuccessSignature) + 1 + len(name) // Assumes len(SuccessSignature) == len(FailureSignature)

	m := iomisc.NewPatternMatchingReader(testOutput, resultSignatureRE, resultMatchLength)
	match, err := iomisc.ReadUntilMatch(ctx, m, nil)
	if err != nil {
		return false, fmt.Errorf("unable to derive test result from runtests output: %w", err)
	}
	return strings.HasPrefix(string(match), SuccessSignature), nil
}
