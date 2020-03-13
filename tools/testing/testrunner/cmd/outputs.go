// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap/lib"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

// testOutput manages the test runner's output drivers. Upon completion, if tar output is
// initialized, a TAR archive containing all other outputs is produced.
type testOutputs struct {
	outDir  string
	summary runtests.TestSummary
	tap     *tap.Producer
}

func createTestOutputs(producer *tap.Producer, outDir string) (*testOutputs, error) {
	return &testOutputs{
		outDir: outDir,
		tap:    producer,
	}, nil
}

// Record writes the test result to initialized outputs.
func (o *testOutputs) record(result testrunner.TestResult) error {
	outputRelPath := filepath.Join(result.Name, runtests.TestOutputFilename)
	// Strip any leading //.
	outputRelPath = strings.TrimLeft(outputRelPath, "//")

	duration := result.EndTime.Sub(result.StartTime)
	if duration <= 0 {
		return fmt.Errorf("test %q must have positive duration: (start, end) = (%v, %v)", result.Name, result.StartTime, result.EndTime)
	}

	o.summary.Tests = append(o.summary.Tests, runtests.TestDetails{
		Name:       result.Name,
		GNLabel:    result.GNLabel,
		OutputFile: outputRelPath,
		Result:     result.Result,
		StartTime:  result.StartTime,
		DurationMillis: duration.Milliseconds(),
		DataSinks:      result.DataSinks,
	})

	desc := fmt.Sprintf("%s (%v)", result.Name, duration)
	o.tap.Ok(result.Result == runtests.TestSuccess, desc)

	if o.outDir != "" {
		stdout := bytes.NewReader(result.Stdout)
		stderr := bytes.NewReader(result.Stderr)
		stdio := io.MultiReader(stdout, stderr)
		outputRelPath = filepath.Join(o.outDir, outputRelPath)
		pathWriter, err := osmisc.CreateFile(outputRelPath)
		if err != nil {
			return fmt.Errorf("failed to create file: %v", err)
		}
		defer pathWriter.Close()
		if _, err := io.Copy(pathWriter, stdio); err != nil {
			return fmt.Errorf("failed to write stdio file for test %q: %v", result.Name, err)
		}
	}
	return nil
}

// Close stops the recording of test outputs; it must be called to finalize them.
func (o *testOutputs) Close() error {
	if o.outDir == "" {
		return nil
	}
	summaryBytes, err := json.Marshal(o.summary)
	if err != nil {
		return err
	}
	summaryPath := filepath.Join(o.outDir, runtests.TestSummaryFilename)
	s, err := osmisc.CreateFile(summaryPath)
	if err != nil {
		return fmt.Errorf("failed to create file: %v", err)
	}
	defer s.Close()
	_, err = io.Copy(s, bytes.NewBuffer(summaryBytes))
	return err
}
