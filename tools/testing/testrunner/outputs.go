// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/url"
	"os"
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

func CreateTestOutputs(producer *tap.Producer, outdir string) (*TestOutputs, error) {
	if outdir == "" {
		return nil, fmt.Errorf("outdir must be set")
	}
	return &TestOutputs{
		OutDir: outdir,
		tap:    producer,
	}, nil
}

// moveOutputFiles takes the list of outputFiles and moves them to newRelDir.
// If an `output file` refers to a directory, the files in that directory will
// be moved to newRelDir while preserving the directory's structure, but the
// individual files will be returned in the list of renamed outputs.
func (o *TestOutputs) moveOutputFiles(outputFiles []string, outputDir string, newRelDir string) ([]string, error) {
	var movedOutputs []string
	for _, outputFilePath := range outputFiles {
		outputFilePath = filepath.Join(outputDir, outputFilePath)
		if err := filepath.Walk(outputFilePath, func(path string, info fs.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if !info.IsDir() {
				oldPathRel, err := filepath.Rel(outputDir, path)
				if err != nil {
					return fmt.Errorf("failed to get relative path of %s to %s: %w", path, outputDir, err)
				}
				newPathRel := filepath.Join(newRelDir, oldPathRel)
				newPathAbs := filepath.Join(o.OutDir, newPathRel)
				os.MkdirAll(filepath.Dir(newPathAbs), os.ModePerm)
				if err := os.Rename(path, newPathAbs); err != nil {
					return fmt.Errorf("failed to move %s to %s: %w", path, newPathAbs, err)
				}
				movedOutputs = append(movedOutputs, newPathRel)
			}
			return nil
		}); err != nil {
			return nil, err
		}
	}
	return movedOutputs, nil
}

// Record writes the test result to initialized outputs.
func (o *TestOutputs) Record(ctx context.Context, result TestResult) error {
	// Sponge doesn't seem to like the path if we just put Name in there.
	nameForPath := url.PathEscape(strings.ReplaceAll(result.Name, ":", ""))
	outputRelPath := filepath.Join(nameForPath, strconv.Itoa(result.RunIndex))
	// Strip any leading //.
	outputRelPath = strings.TrimLeft(outputRelPath, "//")

	stdioPath := filepath.Join(outputRelPath, runtests.TestOutputFilename)

	duration := result.Duration()
	if duration < 0 {
		return fmt.Errorf("test %q must have non-negative duration: (start, end) = (%s, %s)", result.Name, result.StartTime, result.EndTime)
	}

	// Move outputs from test over into the relative path.
	suiteOutputFiles, err := o.moveOutputFiles(result.OutputFiles, result.OutputDir, outputRelPath)
	if err != nil {
		return fmt.Errorf("error moving output files: %w", err)
	}
	containsStdio := false
	for _, outputFile := range suiteOutputFiles {
		if outputFile == stdioPath {
			containsStdio = true
			break
		}
	}
	if !containsStdio {
		suiteOutputFiles = append(suiteOutputFiles, stdioPath)
	}

	var cases []runtests.TestCaseResult
	for i, testCase := range result.Cases {
		// TODO(ihuh): Using the testCase.DisplayName in the new path name
		// can cause errors if the name is too long. Find a better way to
		// display test case output files.
		nameForPath := fmt.Sprintf("case%d", i+1)
		caseRelPath := filepath.Join(outputRelPath, nameForPath)
		caseOutputFiles, err := o.moveOutputFiles(testCase.OutputFiles, testCase.OutputDir, caseRelPath)
		if err != nil {
			return fmt.Errorf("error moving output files: %w", err)
		}
		newCase := testCase
		newCase.OutputFiles = caseOutputFiles
		newCase.OutputDir = ""
		cases = append(cases, newCase)
	}

	// If the stdout/stderr file didn't already exist in the test result's OutputFiles,
	// create it using the bytes from the test Stdio.
	if !containsStdio {
		stdioPath := filepath.Join(o.OutDir, stdioPath)
		pathWriter, err := osmisc.CreateFile(stdioPath)
		if err != nil {
			return fmt.Errorf("failed to create stdio file for test %q: %w", result.Name, err)
		}
		defer pathWriter.Close()
		if _, err := pathWriter.Write(result.Stdio); err != nil {
			return fmt.Errorf("failed to write stdio file for test %q: %w", result.Name, err)
		}
	}

	// Only append the test summary after writing all output files to disk. This
	// ensures that even if writing the output files fails, the summary won't
	// reference nonexistent files.
	o.Summary.Tests = append(o.Summary.Tests, runtests.TestDetails{
		Name:           result.Name,
		GNLabel:        result.GNLabel,
		OutputFiles:    suiteOutputFiles,
		Result:         result.Result,
		Cases:          cases,
		StartTime:      result.StartTime,
		DurationMillis: duration.Milliseconds(),
		DataSinks:      result.DataSinks.Sinks,
		Affected:       result.Affected,
	})

	desc := fmt.Sprintf("%s (%s)", result.Name, duration)
	o.tap.Ok(result.Passed(), desc)

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
