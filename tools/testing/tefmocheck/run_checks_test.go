// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"path"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"

	"github.com/google/go-cmp/cmp"
)

type alwaysTrueCheck struct{}

func (c alwaysTrueCheck) Check(*TestingOutputs) bool {
	return true
}

func (c alwaysTrueCheck) Name() string {
	return "always_true"
}

type alwaysFalseCheck struct{}

func (c alwaysFalseCheck) Check(*TestingOutputs) bool {
	return false
}

func (c alwaysFalseCheck) Name() string {
	return "always_false"
}

type alwaysPanicCheck struct{}

func (c alwaysPanicCheck) Check(*TestingOutputs) bool {
	panic("oh dear")
}

func (c alwaysPanicCheck) Name() string {
	return "always_panic"
}

func TestRunChecks(t *testing.T) {
	falseCheck := alwaysFalseCheck{}
	trueCheck := alwaysTrueCheck{}
	panicCheck := alwaysPanicCheck{}
	checks := []FailureModeCheck{
		falseCheck, trueCheck, panicCheck,
	}
	want := []runtests.TestDetails{
		{
			Name:   path.Join(checkTestNamePrefix, falseCheck.Name()),
			Result: runtests.TestSuccess,
		},
		{
			Name:   path.Join(checkTestNamePrefix, trueCheck.Name()),
			Result: runtests.TestFailure,
		},
		{
			Name:   path.Join(checkTestNamePrefix, panicCheck.Name()),
			Result: runtests.TestSuccess,
		},
	}

	got := RunChecks(checks, nil)
	if diff := cmp.Diff(want, got); diff != "" {
		t.Errorf("RunChecks() returned unexpected tests (-want +got):\n%s", diff)
	}
}
