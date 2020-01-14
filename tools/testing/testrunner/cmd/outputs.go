// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/tarutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap/lib"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

// testOutput manages the test runner's output drivers. Upon completion, if tar output is
// initialized, a TAR archive containing all other outputs is produced.
type testOutputs struct {
	dataDir string
	summary runtests.TestSummary
	tap     *tap.Producer
	tw      *tar.Writer
}

func createTestOutputs(producer *tap.Producer, dataDir, archivePath string) (*testOutputs, error) {
	var tw *tar.Writer
	if archivePath != "" {
		f, err := os.Create(archivePath)
		if err != nil {
			return nil, fmt.Errorf("failed to create file %q: %v", archivePath, err)
		}
		tw = tar.NewWriter(f)
	}

	return &testOutputs{
		dataDir: dataDir,
		tap:     producer,
		tw:      tw,
	}, nil
}

// Record writes the test result to initialized outputs.
func (o *testOutputs) record(result testrunner.TestResult) error {
	pathInArchive := filepath.Join(result.Name, runtests.TestOutputFilename)
	// Strip any leading //.
	pathInArchive = strings.TrimLeft(pathInArchive, "//")

	duration := result.EndTime.Sub(result.StartTime)
	if duration <= 0 {
		return fmt.Errorf("test %q must have positive duration: (start, end) = (%v, %v)", result.Name, result.StartTime, result.EndTime)
	}

	o.summary.Tests = append(o.summary.Tests, runtests.TestDetails{
		Name:       result.Name,
		GNLabel:    result.GNLabel,
		OutputFile: pathInArchive,
		Result:     result.Result,
		StartTime:  result.StartTime,
		// TODO(fxbug.dev/43518): when 1.13 is available, spell this as `duration.Milliseconds()`.
		DurationMillis: duration.Nanoseconds() / (1000 * 1000),
		DataSinks:      result.DataSinks,
	})

	desc := fmt.Sprintf("%s (%v)", result.Name, duration)
	o.tap.Ok(result.Result == runtests.TestSuccess, desc)

	if o.tw != nil {
		stdout := bytes.NewReader(result.Stdout)
		stderr := bytes.NewReader(result.Stderr)
		stdio := io.MultiReader(stdout, stderr)
		size := stdout.Size() + stderr.Size()
		if err := tarutil.TarFromReader(o.tw, stdio, pathInArchive, size); err != nil {
			return fmt.Errorf("failed to stdio file for test %q: %v", result.Name, err)
		}

		for _, sinks := range result.DataSinks {
			for _, sink := range sinks {
				sinkSrc := filepath.Join(o.dataDir, sink.File)
				if err := tarutil.TarFile(o.tw, sinkSrc, sink.File); err != nil {
					return fmt.Errorf("failed to tar data sink %q: %v", sink.Name, err)
				}
			}
		}
	}
	return nil
}

// Close stops the recording of test outputs; it must be called to finalize them.
func (o *testOutputs) Close() error {
	if o.tw == nil {
		return nil
	}
	bytes, err := json.Marshal(o.summary)
	if err != nil {
		return err
	}
	if err := tarutil.TarBytes(o.tw, bytes, runtests.TestSummaryFilename); err != nil {
		return err
	}
	return o.tw.Close()
}
