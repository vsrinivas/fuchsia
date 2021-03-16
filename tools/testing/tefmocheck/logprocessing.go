// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"bufio"
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
			if err = os.MkdirAll(filepath.Dir(destPath), 0766); err != nil {
				return err
			}
			if err := ioutil.WriteFile(destPath, testLog.Bytes, 0666); err != nil {
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

// scanLines is similar to bufio.ScanLines, but it keeps the newlines.
func scanLines(data []byte, atEOF bool) (int, []byte, error) {
	if atEOF && len(data) == 0 {
		return 0, nil, nil
	}
	if i := bytes.IndexByte(data, '\n'); i >= 0 {
		return i + 1, data[0 : i+1], nil
	}
	// If we're at EOF, return remaining data as the final (non-terminated) line.
	if atEOF {
		return len(data), data, nil
	}
	// Ask for more data.
	return 0, nil, nil
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
	scanner := bufio.NewScanner(bytes.NewReader(input))
	var testName string
	var totalTests, testsSeen uint64
	var sawTAPVersion bool
	splitLog := func(data []byte, atEOF bool) (advance int, chunk []byte, retErr error) {
		if atEOF && len(data) == 0 {
			return 0, nil, nil
		}
		// Skip the prefix.
		if !sawTAPVersion {
			tapVersionBytes := []byte("TAP version 13\n")
			tapVersionIndex := bytes.Index(data, tapVersionBytes)
			if tapVersionIndex == -1 {
				return len(data), nil, nil
			}
			sawTAPVersion = true
			return tapVersionIndex + len(tapVersionBytes), nil, nil
		}
		// Clear so that if we see a truncated log, the caller doesn't associate the
		// last chunk with the previous test.
		testName = ""
		origData := data
		// Do not require this to be at start-of-line as it sometimes appears in the middle.
		testFinishedRE, err := regexp.Compile(fmt.Sprintf(`(not )?ok %d (\S+) \(\d+`, testsSeen+1))
		if err != nil {
			return 0, nil, fmt.Errorf("failed to compile regexp: %v", err)
		}
		const testFinishedSubmatches = 3
		for len(data) > 0 {
			advanceForLine, line, err := scanLines(data, atEOF)
			if err != nil {
				return 0, nil, err
			}
			if advanceForLine == 0 && line == nil {
				// data did not contain a complete line, ask for more data.
				return 0, nil, nil
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
					retErr = fmt.Errorf("failed to parse totalTests: %v", err)
				}
				// We've seen everything before the first line of the first test
				break
			} else {
				matches := testFinishedRE.FindSubmatch(line)
				if len(matches) != testFinishedSubmatches {
					continue
				}
				testName = string(matches[testFinishedSubmatches-1])
				// origData[:advance] now includes the logs for testName
				break
			}
		}
		return advance, origData[:advance], retErr
	}
	scanner.Split(splitLog)
	if len(input) > 0 {
		// Prepare for worst case: we can't split the input at all.
		scanner.Buffer(nil, len(input))
	}
	var ret []TestLog
	for scanner.Scan() {
		if testName == "" {
			continue
		}
		// Copy the bytes since "The underlying array may point to data that will
		// be overwritten by a subsequent call to Scan".
		// TODO(garymm): Stop using Scanner so that we can avoid these copies.
		ret = append(ret, TestLog{testName, make([]byte, len(scanner.Bytes())), ""})
		copy(ret[int(testsSeen)].Bytes, scanner.Bytes())
		testsSeen++
	}
	return ret, scanner.Err()
}
