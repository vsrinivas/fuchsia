// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	_ "embed"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

const validLog = `2020/03/20 22:47:38 Attempting to powercycle device upper-bacon-clock-snort
2020/03/20 22:47:38 Using AMT to powercycle.
[0.000] 00000.00000>
[0.000] 00000.00000> welcome to Zircon
[0.000] 00000.00000>
TAP version 13
1..5
Test/1 output line 1
Test/1 output line 2
ok 1 Test/1 (12.6s)
not ok 2 Test2 (44.511025786s)
Test3 output line 1
Test3 output line 2
Tests took 41 seconds.
ok 3 Test3 (27.90584341s)
Test4 output line 1
Test4 output line 2
Test4 output mixed with testrunner output not ok 4 Test4 (88.8s)
Test5 output line 1
fail message
Test5 failed
not ok 5 Test5 (27.90584341s)
Test5 output
2020/03/20 22:57:53.569095 botanist attempting to close SSH session due to: context canceled
ok 6 Test5 (9s)
2020/03/20 22:57:53.569333 botanist DEBUG: stopping or rebooting the node "upper-bacon-clock-snort"
`

var validLogTests = []string{"Test/1", "Test2", "Test3", "Test4", "Test5", "Test5"}

const validFFXLog = `2020/03/20 22:47:38 Attempting to powercycle device upper-bacon-clock-snort
2020/03/20 22:47:38 Using AMT to powercycle.
[0.000] 00000.00000>
[0.000] 00000.00000> welcome to Zircon
[0.000] 00000.00000>
TAP version 13
1..5
testrunner output
Running test 'ffxTest1'
ffxTest1 output line 1
Running test 'ffxTest1.testcase1'
ffxTest1 output line 2
ffxTest1 completed with result: PASSED
Running test 'ffxTest2'
ffxTest2 completed with result: FAILED
ok 1 ffxTest1 (12.6s)
not ok 2 ffxTest2 (44.511025786s)
testrunner output
Running test 'v1Test3'
v1Test3 output line 1
ok 3 fakelog
v1Test3 output line 2
Tests took 41 seconds.
ok 3 v1Test3 (27.90584341s)
hostTest4 output line 1
hostTest4 output line 2
Running test 'test5' inside host test
test5 completed with result: PASSED
hostTest4 output mixed with testrunner output not ok 4 hostTest4 (88.8s)
Running test 'test5'
ok 5 test5 (7s)
Running test 'notRecordedTest'
ok 6 notRecordedTest (234.234s)
2020/03/20 22:57:53.569333 botanist DEBUG: stopping or rebooting the node "upper-bacon-clock-snort"
`

func TestSplitLogByTest(t *testing.T) {
	cases := []struct {
		name           string
		input          string
		inputTests     []string
		expectedOutput []TestLog
	}{
		{
			name:       "log without ffx tests",
			input:      validLog,
			inputTests: validLogTests,
			expectedOutput: []TestLog{
				{"Test/1", []byte("Test/1 output line 1\n" +
					"Test/1 output line 2\n" +
					"ok 1 Test/1 (12.6s)\n"), ""},
				{"Test2", []byte("not ok 2 Test2 (44.511025786s)\n"), ""},
				{"Test3", []byte("Test3 output line 1\n" +
					"Test3 output line 2\n" +
					"Tests took 41 seconds.\n" +
					"ok 3 Test3 (27.90584341s)\n"), ""},
				{"Test4", []byte("Test4 output line 1\n" +
					"Test4 output line 2\n" +
					"Test4 output mixed with testrunner output not ok 4 Test4 (88.8s)\n"), ""},
				{"Test5", []byte("Test5 output line 1\n" +
					"fail message\n" +
					"Test5 failed\n" +
					"not ok 5 Test5 (27.90584341s)\n"), ""},
				{"Test5", []byte("Test5 output\n" +
					"2020/03/20 22:57:53.569095 botanist attempting to close SSH session due to: context canceled\n" +
					"ok 6 Test5 (9s)\n"), ""},
			},
		}, {
			name:       "log with ffx tests",
			input:      validFFXLog,
			inputTests: []string{"ffxTest1", "ffxTest2", "v1Test3", "hostTest4", "test5"},
			expectedOutput: []TestLog{
				{"ffxTest1", []byte("testrunner output\n" +
					"Running test 'ffxTest1'\n" +
					"ffxTest1 output line 1\n" +
					"Running test 'ffxTest1.testcase1'\n" +
					"ffxTest1 output line 2\n" +
					"ffxTest1 completed with result: PASSED\n"), ""},
				{"ffxTest2", []byte("Running test 'ffxTest2'\n" +
					"ffxTest2 completed with result: FAILED\n" +
					"ok 1 ffxTest1 (12.6s)\n" +
					"not ok 2 ffxTest2 (44.511025786s)\n"), ""},
				{"v1Test3", []byte("testrunner output\n" +
					"Running test 'v1Test3'\n" +
					"v1Test3 output line 1\n" +
					"ok 3 fakelog\n" +
					"v1Test3 output line 2\n" +
					"Tests took 41 seconds.\n" +
					"ok 3 v1Test3 (27.90584341s)\n"), ""},
				{"hostTest4", []byte("hostTest4 output line 1\n" +
					"hostTest4 output line 2\n" +
					"Running test 'test5' inside host test\n" +
					"test5 completed with result: PASSED\n" +
					"hostTest4 output mixed with testrunner output not ok 4 hostTest4 (88.8s)\n"), ""},
				{"test5", []byte("Running test 'test5'\n" +
					"ok 5 test5 (7s)\n"), ""},
			},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			output, err := splitLogByTest([]byte(tc.input), tc.inputTests)
			if err != nil {
				t.Fatal("splitLogByTest() returned unexpected error:", err)
			}
			if diff := cmp.Diff(tc.expectedOutput, output); diff != "" {
				t.Errorf("splitLogByTest() returned wrong output (-want +got):\n%s", diff)
			}
		})
	}
}

const truncatedLog = `TAP version 13
1..2
Test1 output line 1
ok 1 Test1 (12.6s)
foo
foo
foo
foo
foo
foo
foo
foo
foo`

func TestSplitTruncatedLog(t *testing.T) {
	input := []byte(truncatedLog)
	output, err := splitLogByTest(input, []string{"Test1", "Test2"})
	if err != nil {
		t.Fatal("splitLogByTest() returned unexpected error:", err)
	}
	wantOutput := []TestLog{
		0: {"Test1", []byte("Test1 output line 1\n" +
			"ok 1 Test1 (12.6s)\n"), ""},
	}
	if diff := cmp.Diff(wantOutput, output); diff != "" {
		t.Errorf("splitLogByTest() returned wrong output (-want +got):\n%s", diff)
	}
}

func TestSplitNonTAPLog(t *testing.T) {
	var input []byte
	// Input does not contain tapVersionBytes.
	for i := 0; i < 200; i++ {
		input = append(input, []byte("foo\n")...)
	}
	output, err := splitLogByTest(input, []string{"foo"})
	if err != nil {
		t.Fatal("splitLogByTest() returned unexpected error:", err)
	}
	if len(output) != 0 {
		t.Errorf("splitLogByTest() returned %d keys, want 0", len(output))
	}
}

func lastLine(input []byte) string {
	ret := strings.TrimRight(string(input), "\n")
	i := strings.LastIndexByte(ret, '\n')
	if i >= 0 {
		ret = ret[i+1:]
	}
	return ret
}

func TestSplitTestLogs(t *testing.T) {
	logDir := t.TempDir()
	if err := os.MkdirAll(logDir, 0o700); err != nil {
		t.Fatal("failed to create unifiedLogDir:", err)
	}

	const baseName = "valid.txt"
	testLogs, err := SplitTestLogs([]byte(validLog), baseName, logDir, validLogTests)
	if err != nil {
		t.Fatal("SplitTestLogs() failed:", err)
	}
	if len(testLogs) == 0 {
		t.Fatal("SplitTestLogs() returned an empty slice")
	}
	for testIndex, testLog := range testLogs {
		if !strings.HasPrefix(testLog.FilePath, logDir) {
			t.Errorf("expected per-test log path to start with baseDir. got: %s\nwant: %s", testLog.FilePath, logDir)
		}
		if filepath.Base(testLog.FilePath) != baseName {
			t.Errorf("expected per-test log path to end with baseName. got: %s\n want: %s", testLog.FilePath, baseName)
		}
		actualLastLine := lastLine(testLog.Bytes)
		expectedRE, err := regexp.Compile(fmt.Sprintf("(not )?ok %d ", testIndex+1))
		if err != nil {
			t.Errorf("failed to compile regexp for expected last line: %v", err)
		} else if !expectedRE.Match([]byte(actualLastLine)) {
			t.Errorf("wrong final log line. got:%s\nwant:%s", actualLastLine, expectedRE.String())
		}
	}
}
