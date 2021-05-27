// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestRunNinja(t *testing.T) {
	ctx := context.Background()

	testCases := []struct {
		name string
		// Whether to mock a non-zero ninja retcode, in which case we should get
		// an error.
		fail bool
		// Mock Ninja stdout.
		stdout                 string
		expectedFailureMessage string
	}{
		{
			name: "success",
			stdout: `
                [1/2] ACTION a.o
                [2/2] ACTION b.o
            `,
		},
		{
			name: "single failed target",
			fail: true,
			stdout: `
                [35792/53672] CXX a.o b.o
                [35793/53672] CXX c.o d.o
                FAILED: c.o d.o
                output line 1
                output line 2
                [35794/53672] CXX successful/e.o
                [35795/53672] CXX f.o
            `,
			expectedFailureMessage: `
                [35793/53672] CXX c.o d.o
                FAILED: c.o d.o
                output line 1
                output line 2
            `,
		},
		{
			name: "preserves indentation",
			fail: true,
			stdout: `
                [35793/53672] CXX a.o b.o
                FAILED: a.o b.o
                    output line 1
                        output line 2
                            output line 3
                [35794/53672] CXX successful/c.o
            `,
			expectedFailureMessage: `
                [35793/53672] CXX a.o b.o
                FAILED: a.o b.o
                    output line 1
                        output line 2
                            output line 3
            `,
		},
		{
			name: "multiple failed targets",
			fail: true,
			stdout: `
                [35790/53672] CXX foo
                [35791/53672] CXX a.o b.o
                FAILED: a.o b.o
                output line 1
                output line 2
                [35792/53672] CXX c.o d.o
                [35793/53673] CXX e.o
                FAILED: e.o
                output line 3
                output line 4
                [35794/53672] CXX f.o
            `,
			expectedFailureMessage: `
                [35791/53672] CXX a.o b.o
                FAILED: a.o b.o
                output line 1
                output line 2
                [35793/53673] CXX e.o
                FAILED: e.o
                output line 3
                output line 4
            `,
		},
		{
			name: "last target fails",
			fail: true,
			stdout: `
                [35790/53672] CXX foo
                [35791/53672] CXX a.o b.o
                FAILED: a.o b.o
                output line 1
                output line 2
                ninja: build stopped: subcommand failed.
            `,
			expectedFailureMessage: `
                [35791/53672] CXX a.o b.o
                FAILED: a.o b.o
                output line 1
                output line 2
            `,
		},
		{
			name: "graph error",
			fail: true,
			stdout: `
				ninja: Entering directory /foo
				ninja: error: bar.ninja: multiple rules generate baz
            `,
			expectedFailureMessage: `
				ninja: error: bar.ninja: multiple rules generate baz
            `,
		},
		{
			name: "fatal error",
			fail: true,
			stdout: `
				ninja: Entering directory /foo
				[1/1] ACTION //foo
				ninja: fatal: cannot create file foo
            `,
			expectedFailureMessage: `
				ninja: fatal: cannot create file foo
            `,
		},
		{
			name: "unrecognized failure",
			fail: true,
			stdout: `
				ninja: Entering directory /foo
				...something went wrong...
            `,
			expectedFailureMessage: unrecognizedFailureMsg,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			tc.stdout = normalize(tc.stdout)
			tc.expectedFailureMessage = normalize(tc.expectedFailureMessage)

			sr := &fakeSubprocessRunner{
				mockStdout: []byte(tc.stdout),
				fail:       tc.fail,
			}
			r := ninjaRunner{
				runner:    sr,
				ninjaPath: filepath.Join(t.TempDir(), "ninja"),
				buildDir:  filepath.Join(t.TempDir(), "out"),
				jobCount:  23, // Arbitrary but distinctive value.
			}
			msg, err := runNinja(ctx, r, []string{"foo", "bar"}, false)
			if tc.fail {
				if !errors.Is(err, errSubprocessFailure) {
					t.Fatalf("Expected a subprocess failure error but got: %s", err)
				}
			} else if err != nil {
				t.Fatalf("Unexpected error: %s", err)
			}

			if len(sr.commandsRun) != 1 {
				t.Fatalf("expected runNinja to run 1 command but got %d", len(sr.commandsRun))
			}
			cmd := sr.commandsRun[0]
			if cmd[0] != r.ninjaPath {
				t.Fatalf("runNinja ran wrong executable %q (expected %q)", cmd[0], r.ninjaPath)
			}
			foundJobCount := false
			for i, part := range cmd {
				if part == "-j" {
					foundJobCount = true
					if i+1 >= len(cmd) || cmd[i+1] != fmt.Sprintf("%d", r.jobCount) {
						t.Errorf("wrong value for -j flag: %v", cmd)
					}
				}
			}
			if !foundJobCount {
				t.Errorf("runNinja didn't set the -j flag. Full command: %v", cmd)
			}

			if diff := cmp.Diff(tc.expectedFailureMessage, msg); diff != "" {
				t.Errorf("Unexpected failure message diff (-want +got):\n%s", diff)
			}
		})
	}
}

// normalize removes a leading newline and trailing spaces from a multiline
// string, ensuring that the expected failure message has the same whitespace
// formatting as failure messages emitted by runNinja.
func normalize(s string) string {
	s = strings.TrimLeft(s, "\n")
	s = strings.TrimRight(s, " ")
	return s
}

func TestCheckNinjaNoop(t *testing.T) {
	testCases := []struct {
		name       string
		isMac      bool
		stdout     string
		expectNoop bool
	}{
		{
			name:       "no-op",
			stdout:     "ninja: Entering directory /foo\nninja: no work to do.",
			expectNoop: true,
		},
		{
			name:       "dirty",
			stdout:     "ninja: Entering directory /foo\n[1/1] STAMP foo.stamp",
			expectNoop: false,
		},
		{
			name:       "mac dirty",
			isMac:      true,
			stdout:     "ninja: Entering directory /foo\n[1/1] STAMP foo.stamp",
			expectNoop: false,
		},
		{
			name:       "broken mac path",
			isMac:      true,
			stdout:     "ninja: Entering directory /foo\nninja explain: ../../../../usr/bin/env is dirty",
			expectNoop: true,
		},
	}

	ctx := context.Background()
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			r := ninjaRunner{
				runner: &fakeSubprocessRunner{
					mockStdout: []byte(tc.stdout),
				},
				ninjaPath: "ninja",
				buildDir:  t.TempDir(),
			}
			noop, logFiles, err := checkNinjaNoop(ctx, r, []string{"foo"}, tc.isMac)
			if err != nil {
				t.Fatal(err)
			}
			if noop != tc.expectNoop {
				t.Fatalf("Unexpected ninja no-op result: got %v, expected %v", noop, tc.expectNoop)
			}
			if tc.expectNoop {
				if len(logFiles) > 0 {
					t.Errorf("Expected no log files in case of no-op, but got: %+v", logFiles)
				}
			} else if len(logFiles) != 2 {
				t.Errorf("Expected 2 log files in case of non-no-op, but got: %+v", logFiles)
			}
		})
	}
}

func TestNinjaGraph(t *testing.T) {
	ctx := context.Background()
	stdout := "ninja\ngraph\nstdout"
	r := ninjaRunner{
		runner: &fakeSubprocessRunner{
			mockStdout: []byte(stdout),
		},
		ninjaPath: "ninja",
		buildDir:  t.TempDir(),
	}
	path, err := ninjaGraph(ctx, r, []string{"foo", "bar"})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(path)
	fileContentsBytes, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}

	fileContents := string(fileContentsBytes)
	if diff := cmp.Diff(stdout, fileContents); diff != "" {
		t.Errorf("Unexpected ninja graph file diff (-want +got):\n%s", diff)
	}
}

func TestNinjaCompdb(t *testing.T) {
	ctx := context.Background()
	stdout := "ninja\ncompdb\nstdout"
	r := ninjaRunner{
		runner: &fakeSubprocessRunner{
			mockStdout: []byte(stdout),
		},
		ninjaPath: "ninja",
		buildDir:  t.TempDir(),
	}
	path, err := ninjaCompdb(ctx, r)
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(path)
	fileContentsBytes, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}

	fileContents := string(fileContentsBytes)
	if diff := cmp.Diff(stdout, fileContents); diff != "" {
		t.Errorf("Unexpected ninja compdb file diff (-want +got):\n%s", diff)
	}
}
