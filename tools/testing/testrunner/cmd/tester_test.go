// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"

	"golang.org/x/crypto/ssh"
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

// Verifies that SSHTester can execute tests on a remote device. These tests are
// only meant for local verification.  You can execute them like this:
//
//  FUCHSIA_NODENAME=<my nodename> FUCHSIA_SSH_KEY=<my key> go test ./...
func TestSSHTester(t *testing.T) {
	t.Skip("ssh tests are meant for local testing only")

	nodename := os.Getenv("FUCHSIA_NODENAME")
	if nodename == "" {
		t.Fatal("FUCHSIA_NODENAME not set")
	}
	sshKeyFile := os.Getenv("FUCHSIA_SSH_KEY")
	if sshKeyFile == "" {
		t.Fatal("FUCHSIA_SSH_KEY not set")
	}
	sshKey, err := ioutil.ReadFile(sshKeyFile)
	if err != nil {
		t.Fatalf("could not read file %q", sshKeyFile)
	}

	cases := []struct {
		name   string
		tests  []build.Test
		stdout string
		stderr string
	}{
		{
			name: "should run a command over SSH",
			tests: []build.Test{
				{
					Name: "hello_world_test",
					// Just 'echo' and not '/bin/echo' because this assumes we're running on
					// Fuchsia.
					Command: []string{"echo", "Hello world!"},
				},
			},
			stdout: "Hello world!",
		},
		{
			name: "should run successive commands over SSH",
			tests: []build.Test{
				{
					Name:    "test_1",
					Command: []string{"echo", "this is test 1"},
				},
				{
					Name:    "test_2",
					Command: []string{"echo", "this is test 2"},
				},
			},
			stdout: "this is test 1\nthis is test 2",
		},
	}

	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			newClient := func(ctx context.Context) (*ssh.Client, error) {
				config, err := sshutil.DefaultSSHConfig(sshKey)
				if err != nil {
					return nil, fmt.Errorf("failed to create an SSH client config: %v", err)
				}
				client, err := sshutil.ConnectToNode(ctx, nodename, config)
				if err != nil {
					return nil, fmt.Errorf("failed to connect to node %q: %v", nodename, err)
				}
				return client, nil
			}

			tester, err := NewSSHTester(newClient)
			if err != nil {
				t.Errorf("failed to intialize tester: %v", err)
				return
			}

			stdout := new(bytes.Buffer)
			stderr := new(bytes.Buffer)
			for _, test := range tt.tests {
				if _, err := tester.Test(context.Background(), test, stdout, stderr); err != nil {
					t.Error(err)
					return
				}
			}

			if err := tester.Close(); err != nil {
				t.Fatalf("failed to close tester: %v", err)
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
