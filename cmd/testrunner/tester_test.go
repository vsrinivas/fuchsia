// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"strings"
	"testing"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/testsharder"
)

func TestTester(t *testing.T) {
	tester := RunTestInSubprocess
	cases := []testCase{
		{
			name:   "should run a command a local subprocess",
			tester: tester,
			test: testsharder.Test{
				Name: "hello_world_test",
				// Assumes that we're running on a Unix system.
				Command: []string{"/bin/echo", "Hello world!"},
			},
			expectedOutput: "Hello world!",
		},
	}

	runTestCases(t, cases)
}

// Verifies that SSHTester can execute tests on a remote device. These tests are
// only meant for local verification.  You can execute them like this:
//
//  DEVICE_CONTEXT=./device.json go test ./...
func TestSSHTester(t *testing.T) {
	t.Skip("ssh tests are meant for local testing only")

	devCtx, err := botanist.GetDeviceContext()
	if err != nil {
		t.Fatal(err)
	}

	client, err := sshIntoNode(devCtx.Nodename, devCtx.SSHKey)
	if err != nil {
		t.Fatalf("failed to connect to node '%s': %v", devCtx.Nodename, err)
	}

	tester := &SSHTester{client: client}
	cases := []testCase{
		{
			name:   "should run a command over SSH",
			tester: tester.Test,
			test: testsharder.Test{
				Name: "hello_world_test",
				// Just 'echo' and not '/bin/echo' because this assumes we're running on
				// Fuchsia.
				Command: []string{"echo", "Hello world!"},
			},
			expectedOutput: "Hello world!",
		},
		{
			name:   "should run successive commands over SSH",
			tester: tester.Test,
			test: testsharder.Test{
				Name:    "hello_again_test",
				Command: []string{"echo", "Hello again!"},
			},
			expectedOutput: "Hello again!",
		},
	}

	runTestCases(t, cases)
}

type testCase struct {
	name           string
	test           testsharder.Test
	tester         Tester
	expectedOutput string
	expectError    bool
}

func runTestCases(t *testing.T, cases []testCase) {
	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			output := new(bytes.Buffer)
			err := tt.tester(context.Background(), tt.test, output, output)
			if tt.expectError && err == nil {
				t.Fatalf("%s: got nil, wanted error", tt.name)
			} else if !tt.expectError && err != nil {
				t.Fatalf("%s: got err '%v', wanted nil", tt.name, err)
			} else if tt.expectedOutput != strings.TrimSpace(output.String()) {
				t.Fatalf("%s: got output: '%s', want: '%s'", tt.name, output.String(), tt.expectedOutput)
			}
		})
	}
}
