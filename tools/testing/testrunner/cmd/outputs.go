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
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
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
	// Sponge doesn't seem to like the path if we just put Name in there.
	nameForPath := strings.ReplaceAll(result.Name, ":", "")
	nameForPath = strings.ReplaceAll(nameForPath, "#", string(filepath.Separator))
	outputRelPath := filepath.Join(nameForPath, strconv.Itoa(result.RunIndex), runtests.TestOutputFilename)
	// Strip any leading //.
	outputRelPath = strings.TrimLeft(outputRelPath, "//")

	duration := result.EndTime.Sub(result.StartTime)
	if duration <= 0 {
		return fmt.Errorf("test %q must have positive duration: (start, end) = (%v, %v)", result.Name, result.StartTime, result.EndTime)
	}

	o.summary.Tests = append(o.summary.Tests, runtests.TestDetails{
		Name:           result.Name,
		GNLabel:        result.GNLabel,
		OutputFile:     outputRelPath,
		Result:         result.Result,
		Cases:          result.Cases,
		StartTime:      result.StartTime,
		DurationMillis: duration.Milliseconds(),
		DataSinks:      runtests.DataSinkMap(result.DataSinks),
	})

	desc := fmt.Sprintf("%s (%v)", result.Name, duration)
	o.tap.Ok(result.Result == runtests.TestSuccess, desc)

	if o.outDir != "" {
		outputRelPath = filepath.Join(o.outDir, outputRelPath)
		pathWriter, err := osmisc.CreateFile(outputRelPath)
		if err != nil {
			return fmt.Errorf("failed to create stdio file for test %q: %v", result.Name, err)
		}
		defer pathWriter.Close()
		if _, err := pathWriter.Write(result.Stdio); err != nil {
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
