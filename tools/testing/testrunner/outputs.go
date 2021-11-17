// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/url"
	"path/filepath"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
)

// TestOutputs manages the test runner's output drivers. Upon completion, if tar output is
// initialized, a TAR archive containing all other outputs is produced.
type TestOutputs struct {
	OutDir  string
	Summary runtests.TestSummary
	tap     *tap.Producer
}

func CreateTestOutputs(producer *tap.Producer, outDir string) *TestOutputs {
	return &TestOutputs{
		OutDir: outDir,
		tap:    producer,
	}
}

// Record writes the test result to initialized outputs.
func (o *TestOutputs) Record(result TestResult) error {
	// Sponge doesn't seem to like the path if we just put Name in there.
	nameForPath := url.PathEscape(strings.ReplaceAll(result.Name, ":", ""))
	outputRelPath := filepath.Join(nameForPath, strconv.Itoa(result.RunIndex), runtests.TestOutputFilename)
	// Strip any leading //.
	outputRelPath = strings.TrimLeft(outputRelPath, "//")

	duration := result.Duration()
	if duration <= 0 {
		return fmt.Errorf("test %q must have positive duration: (start, end) = (%s, %s)", result.Name, result.StartTime, result.EndTime)
	}

	o.Summary.Tests = append(o.Summary.Tests, runtests.TestDetails{
		Name:           result.Name,
		GNLabel:        result.GNLabel,
		OutputFiles:    []string{outputRelPath},
		Result:         result.Result,
		Cases:          result.Cases,
		StartTime:      result.StartTime,
		DurationMillis: duration.Milliseconds(),
		DataSinks:      result.DataSinks.Sinks,
	})

	desc := fmt.Sprintf("%s (%s)", result.Name, duration)
	o.tap.Ok(result.Passed(), desc)

	if o.OutDir != "" {
		outputRelPath = filepath.Join(o.OutDir, outputRelPath)
		pathWriter, err := osmisc.CreateFile(outputRelPath)
		if err != nil {
			return fmt.Errorf("failed to create stdio file for test %q: %w", result.Name, err)
		}
		defer pathWriter.Close()
		if _, err := pathWriter.Write(result.Stdio); err != nil {
			return fmt.Errorf("failed to write stdio file for test %q: %w", result.Name, err)
		}
	}
	return nil
}

// UpdateDataSinks updates the DataSinks field of the tests in the summary with
// the provided `newSinks`. If the sinks were copied to a subdirectory within
// o.outDir, that path should be provided as the `insertPrefixPath` which will
// get prepended to the sink file paths so that they point to the correct paths
// relative to o.outDir.
func (o *TestOutputs) updateDataSinks(newSinks map[string]runtests.DataSinkReference, insertPrefixPath string) {
	for i, test := range o.Summary.Tests {
		if sinkRef, ok := newSinks[test.Name]; ok {
			if test.DataSinks == nil {
				test.DataSinks = runtests.DataSinkMap{}
			}
			for name, sinks := range sinkRef.Sinks {
				for _, sink := range sinks {
					sink.File = filepath.Join(insertPrefixPath, sink.File)
					test.DataSinks[name] = append(test.DataSinks[name], sink)
				}
			}
			o.Summary.Tests[i] = test
		}
	}
}

// Close stops the recording of test outputs; it must be called to finalize them.
func (o *TestOutputs) Close() error {
	if o.OutDir == "" {
		return nil
	}
	summaryBytes, err := json.Marshal(o.Summary)
	if err != nil {
		return err
	}
	summaryPath := filepath.Join(o.OutDir, runtests.TestSummaryFilename)
	s, err := osmisc.CreateFile(summaryPath)
	if err != nil {
		return fmt.Errorf("failed to create file: %w", err)
	}
	defer s.Close()
	_, err = io.Copy(s, bytes.NewBuffer(summaryBytes))
	return err
}
