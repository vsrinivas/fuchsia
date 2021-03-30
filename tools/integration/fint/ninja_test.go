// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"errors"
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

			r := &fakeSubprocessRunner{
				mockStdout: []byte(tc.stdout),
				fail:       tc.fail,
			}
			ninjaPath := filepath.Join(t.TempDir(), "ninja")
			buildDir := filepath.Join(t.TempDir(), "out")
			msg, err := runNinja(ctx, r, ninjaPath, buildDir, []string{"foo", "bar"})
			if tc.fail {
				if !errors.Is(err, errSubprocessFailure) {
					t.Fatalf("Expected a subprocess failure error but got: %s", err)
				}
			} else if err != nil {
				t.Fatalf("Unexpected error: %s", err)
			}

			if len(r.commandsRun) != 1 {
				t.Fatalf("expected runNinja to run 1 command but got %d", len(r.commandsRun))
			}
			cmd := r.commandsRun[0]
			if cmd[0] != ninjaPath {
				t.Fatalf("runNinja ran wrong executable %q (expected %q)", cmd[0], ninjaPath)
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
