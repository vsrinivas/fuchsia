// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runtests

import (
	"context"
	"fmt"
	"io"
	"regexp"

	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
)

const (
	SuccessSignature = "[runtests][PASSED]"
	FailureSignature = "[runtests][FAILED]"
)

var (
	resultSignatureRE = regexp.MustCompile(fmt.Sprintf("(%s|%s)", regexp.QuoteMeta(SuccessSignature), regexp.QuoteMeta(FailureSignature)))
	resultMatchLength = len(SuccessSignature) // Assumes len(SuccessSignature) == len(FailureSignature)
)

// TestPassed reads in the output from a runtests invocation of a single test
// and returns whether that test succeeded.
func TestPassed(ctx context.Context, testOutput io.Reader) (bool, error) {
	m := iomisc.NewPatternMatchingReader(testOutput, resultSignatureRE, resultMatchLength)
	match, err := iomisc.ReadUntilMatch(ctx, m, nil)
	if err != nil {
		return false, fmt.Errorf("unable to derive test result from runtests output: %w", err)
	}
	return string(match) == SuccessSignature, nil
}
