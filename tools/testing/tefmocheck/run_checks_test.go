// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"os"
	"path"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

type alwaysTrueCheck struct {
	outputsDir string
}

func (c alwaysTrueCheck) Check(*TestingOutputs) bool {
	return true
}

func (c alwaysTrueCheck) Name() string {
	return "always_true"
}

func (c alwaysTrueCheck) DebugText() string {
	return "True dat"
}

func (c alwaysTrueCheck) OutputFiles() []string {
	return []string{filepath.Join(c.outputsDir, "true.txt")}
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

func (c alwaysFalseCheck) OutputFiles() []string {
	return []string{}
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

func (c alwaysPanicCheck) OutputFiles() []string {
	return []string{}
}

func TestRunChecks(t *testing.T) {
	falseCheck := alwaysFalseCheck{}
	outputsDir := t.TempDir()
	trueCheck := alwaysTrueCheck{outputsDir}
	panicCheck := alwaysPanicCheck{}
	checks := []FailureModeCheck{
		falseCheck, trueCheck, panicCheck,
	}
	debugPath := debugPathForCheck(trueCheck)
	want := []runtests.TestDetails{
		{
			Name:                 path.Join(checkTestNamePrefix, trueCheck.Name()),
			Result:               runtests.TestFailure,
			IsTestingFailureMode: true,
			OutputFiles:          []string{debugPath},
		},
	}
	for _, of := range trueCheck.OutputFiles() {
		want[0].OutputFiles = append(want[0].OutputFiles, filepath.Base(of))
	}
	startTime := time.Now()

	got, err := RunChecks(checks, nil, outputsDir)
	if err != nil {
		t.Error("RunChecks() failed with:", err)
	}
	for i, td := range got {
		if td.StartTime.Sub(startTime) < 0 {
			t.Errorf("start time should be later than %v, got %v", startTime, td.StartTime)
		}
		// Since the start time and duration are based on the current time, we should
		// set those values to the default values so that we don't check them when
		// comparing the actual and expected test details.
		var defaultTime time.Time
		got[i].StartTime = defaultTime
		got[i].DurationMillis = 0
		for _, outputFile := range td.OutputFiles {
			// RunChecks() is only responsible for writing the debug text to a file.
			if outputFile != debugPath {
				continue
			}
			if _, err := os.Stat(filepath.Join(outputsDir, outputFile)); err != nil {
				t.Errorf("failed to stat OutputFile %s: %v", outputFile, err)
			}
		}
	}
	if diff := cmp.Diff(want, got, cmpopts.EquateEmpty()); diff != "" {
		t.Errorf("RunChecks() returned unexpected tests (-want +got):\n%s", diff)
	}
}
