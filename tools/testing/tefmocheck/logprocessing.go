// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strconv"

	"golang.org/x/sync/errgroup"
)

// SplitTestLogs splits logBytes into per-test logs,
// writes those per-test logs to files in outDir, and returns a slice of TestLogs.
// The logs will be written to the parent directory of path.
func SplitTestLogs(logBytes []byte, logBaseName, outDir string) ([]TestLog, error) {
	testLogs, err := splitLogByTest(logBytes)
	if err != nil {
		return nil, err
	}

	if outDir == "" {
		return testLogs, nil
	}
	var g errgroup.Group
	for ti := range testLogs {
		testIndex := ti // capture
		g.Go(func() error {
			testLog := &testLogs[testIndex]
			destPath := filepath.Join(outDir, strconv.Itoa(testIndex), logBaseName)
			if err := os.MkdirAll(filepath.Dir(destPath), 0o766); err != nil {
				return err
			}
			if err := ioutil.WriteFile(destPath, testLog.Bytes, 0o666); err != nil {
				return err
			}
			testLog.FilePath = destPath
			return nil
		})
	}
	if err = g.Wait(); err != nil {
		return nil, err
	}
	return testLogs, nil
}

var testPlanRE = regexp.MustCompile(`1\.\.(\d+)\n`)

const testPlanSubmatches = 2

// TestLog represents an individual test's slice of a larger log file.
type TestLog struct {
	TestName string
	Bytes    []byte
	FilePath string
}

// splitLogsByTest expects the standard output of a Fuchsia test Swarming task
// as input. We expect it to look like:
// * Prefix that we ignore
// * TAP version line (TAP = Test Anything Protocol, https://testanything.org/)
// * Alternating log lines and TAP lines.
// * Suffix we that we ignore
//
// NOTE: This is not a correct TAP consumer. There are many things in TAP
// that it does not handle, and there are things not in TAP that it does handle.
// It is only tested against the output of the fuchsia testrunner:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/testing/testrunner
//
// Technical debt: this should probably be moved or combined with code in tools/testing/tap/.
// It exists separately because this code was originally written in google3.
//
// Returns a slice of TestLogs.
func splitLogByTest(input []byte) ([]TestLog, error) {
	var ret []TestLog
	// Everything up to and including tapVersionBytes is a prefix that we skip.
	tapVersionBytes := []byte("TAP version 13\n")
	tapVersionIndex := bytes.Index(input, tapVersionBytes)
	if tapVersionIndex == -1 {
		return ret, nil
	}

	var totalTests, testsSeen uint64
	var advance int
	for i := tapVersionIndex + len(tapVersionBytes); i < len(input); i += advance {
		advance = 0
		data := input[i:]
		// Do not require this to be at start-of-line as it sometimes appears in the middle.
		testFinishedRE, err := regexp.Compile(fmt.Sprintf(`(not )?ok %d (\S+) \(\d+`, testsSeen+1))
		if err != nil {
			return ret, fmt.Errorf("failed to compile regexp: %v", err)
		}
		const testFinishedSubmatches = 3
		for len(data) > 0 {
			advanceForLine, line := len(data), data
			if j := bytes.IndexByte(data, '\n'); j >= 0 {
				advanceForLine, line = j+1, data[0:j+1]
			}
			data = data[advanceForLine:]
			advance += advanceForLine
			if totalTests == 0 {
				matches := testPlanRE.FindSubmatch(line)
				if len(matches) != testPlanSubmatches {
					continue
				}
				totalTests, err = strconv.ParseUint(string(matches[testPlanSubmatches-1]), 10, 64)
				if err != nil {
					return ret, fmt.Errorf("failed to parse totalTests: %v", err)
				}
				// We've seen everything before the first line of the first test
				break
			} else {
				matches := testFinishedRE.FindSubmatch(line)
				if len(matches) != testFinishedSubmatches {
					continue
				}
				ret = append(ret, TestLog{string(matches[testFinishedSubmatches-1]), input[i : i+advance], ""})
				testsSeen += 1
				break
			}
		}
	}
	return ret, nil
}
