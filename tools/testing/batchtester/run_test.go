// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package batchtester

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRun(t *testing.T) {
	passingTest := Test{
		Name:       "passing-test",
		Executable: writeTestScript(t, "pass.sh", true),
	}
	failingTest := Test{
		Name:       "failing-test",
		Executable: writeTestScript(t, "fail.sh", false),
	}

	for _, tc := range []struct {
		name        string
		tests       []Test
		expectedErr string
	}{
		{
			name: "no tests",
		},
		{
			name: "one passing test",
			tests: []Test{
				passingTest,
			},
		},
		{
			name: "one failing test",
			tests: []Test{
				failingTest,
			},
			expectedErr: "1 of 1 test(s) failed",
		},
		{
			name: "one failing test",
			tests: []Test{
				failingTest,
				passingTest,
			},
			expectedErr: "1 of 2 test(s) failed",
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			config := Config{
				Tests: tc.tests,
			}
			if err := Run(context.Background(), &config); err != nil {
				if err.Error() != tc.expectedErr {
					t.Fatal(err)
				}
			} else if tc.expectedErr != "" {
				t.Fatal("Expected an error but Run succeeded")
			}
		})
	}
}

func writeTestScript(t *testing.T, basename string, passing bool) string {
	lines := []string{"#!/bin/sh"}
	if !passing {
		lines = append(lines, "exit 1")
	}
	contents := []byte(strings.Join(lines, "\n") + "\n")
	path := filepath.Join(t.TempDir(), basename)
	if err := os.WriteFile(path, contents, 0o755); err != nil {
		t.Fatal(err)
	}
	return path
}
