// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

type alwaysTrueCheck struct{}

func (c alwaysTrueCheck) Check(*TestingOutputs) bool {
	return true
}

func (c alwaysTrueCheck) Name() string {
	return "always_true"
}

func (c alwaysTrueCheck) DebugText() string {
	return "True dat"
}

type alwaysFalseCheck struct{}

func (c alwaysFalseCheck) Check(*TestingOutputs) bool {
	return false
}

func (c alwaysFalseCheck) Name() string {
	return "always_false"
}

func (c alwaysFalseCheck) DebugText() string {
	return "Lies!"
}

type alwaysPanicCheck struct{}

func (c alwaysPanicCheck) Check(*TestingOutputs) bool {
	panic("oh dear")
}

func (c alwaysPanicCheck) Name() string {
	return "always_panic"
}

func (c alwaysPanicCheck) DebugText() string {
	return ""
}

func TestRunChecks(t *testing.T) {
	falseCheck := alwaysFalseCheck{}
	trueCheck := alwaysTrueCheck{}
	panicCheck := alwaysPanicCheck{}
	checks := []FailureModeCheck{
		falseCheck, trueCheck, panicCheck,
	}
	outputsDir, err := ioutil.TempDir("", "check-outputs")
	if err != nil {
		t.Fatal("failed to create temp outputs dir:", err)
	}
	defer os.RemoveAll(outputsDir)
	want := []runtests.TestDetails{
		{
			Name:                 path.Join(checkTestNamePrefix, falseCheck.Name()),
			Result:               runtests.TestSuccess,
			IsTestingFailureMode: true,
		},
		{
			Name:                 path.Join(checkTestNamePrefix, trueCheck.Name()),
			Result:               runtests.TestFailure,
			IsTestingFailureMode: true,
			OutputFile:           debugPathForCheck(trueCheck),
		},
		{
			Name:                 path.Join(checkTestNamePrefix, panicCheck.Name()),
			Result:               runtests.TestSuccess,
			IsTestingFailureMode: true,
		},
	}

	got, err := RunChecks(checks, nil, outputsDir)
	if err != nil {
		t.Error("RunChecks() failed with:", err)
	}
	if diff := cmp.Diff(want, got, cmpopts.EquateEmpty()); diff != "" {
		t.Errorf("RunChecks() returned unexpected tests (-want +got):\n%s", diff)
	}
	for _, td := range want {
		if td.OutputFile != "" {
			if _, err := os.Stat(filepath.Join(outputsDir, td.OutputFile)); err != nil {
				t.Errorf("failed to stat OutputFile %s: %v", td.OutputFile, err)
			}
		}
	}
}
