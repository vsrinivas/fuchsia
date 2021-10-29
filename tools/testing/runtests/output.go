// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runtests

import (
	"context"
	"fmt"
	"io"

	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
)

const (
	SuccessSignature = "[runtests][PASSED] "
	FailureSignature = "[runtests][FAILED] "
	StartedSignature = "RUNNING TEST: "
)

// TestPassed reads in the output from a runtests invocation of a single test and returns whether
// that test succeeded. The expected signature, eg "[runtests][PASSED] /test/name", must match the
// one in
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/runtests-utils/fuchsia-run-test.cc
func TestPassed(ctx context.Context, testOutput io.Reader, name string) (bool, error) {
	success := SuccessSignature + name
	failure := FailureSignature + name
	match, err := iomisc.ReadUntilMatchString(ctx, testOutput, success, failure)
	if err != nil {
		return false, fmt.Errorf("unable to derive test result from runtests output: %w", err)
	}
	return match == success, nil
}
