// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

func TestTester(t *testing.T) {
	cases := []struct {
		name   string
		test   build.Test
		stdout string
		stderr string
	}{
		{
			name: "should run a command a local subprocess",
			test: build.Test{
				Name:  "hello_world_test",
				Label: "//a/b/c/hello_word:hello_world_test(//toolchain)",
				// Assumes that we're running on a Unix system.
				Command: []string{"/bin/echo", "Hello world!"},
			},
			stdout: "Hello world!",
		},
	}

	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			tester := &SubprocessTester{}

			stdout := new(bytes.Buffer)
			stderr := new(bytes.Buffer)

			dataSinks, err := tester.Test(context.Background(), tt.test, stdout, stderr)
			if err != nil {
				t.Errorf("failed to execute test %q: %v", tt.test.Name, err)
				return
			}
			if dataSinks != nil {
				t.Errorf("got non-nil data sinks: %v", dataSinks)
			}

			// Compare stdout
			actual := strings.TrimSpace(stdout.String())
			expected := tt.stdout
			if actual != expected {
				t.Errorf("got stdout %q but wanted %q", actual, expected)
			}

			// Compare stderr
			actual = strings.TrimSpace(stderr.String())
			expected = tt.stderr
			if actual != expected {
				t.Errorf("got stderr %q but wanted %q", actual, expected)
			}
		})
	}
}
