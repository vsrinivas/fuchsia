// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

// Note: In order to avoid needing to mock out exec.Cmd or to have external
// program(s) act as mock subprocesses, we implement the subprocess mocks here
// in the main test binary (see TestDoProcessMock) and spawn a copy of ourself
// as a subprocess whenever mockCommand is called. A special environment
// variable is set to trigger the mocking behavior, and the first non-flag
// command-line argument is used as the command name so we know which specific
// behavior to emulate. Despite being misleadingly named process_test.go (which
// is necessary so that we can invoke TestDoProcessMock in the subprocess),
// this file is actually mostly process mocking.

// Drop-in replacement for exec.Command for use during testing.
func mockCommand(command string, args ...string) *exec.Cmd {
	// Call ourself as the subprocess so we can mock behavior as appropriate
	argv := []string{"-logtostderr", "-test.run=TestDoProcessMock", "--", command}
	argv = append(argv, args...)

	cmd := exec.Command(os.Args[0], argv...)
	cmd.Env = os.Environ()
	cmd.Env = append(cmd.Env, "MOCK_PROCESS=yes")
	return cmd
}

// This is the actual implementation of mocked subprocesses.
// The name is a little confusing because it has to have the Test prefix
func TestDoProcessMock(t *testing.T) {
	if os.Getenv("MOCK_PROCESS") != "yes" {
		t.Skip("Not in a subprocess")
	}

	cmd, args := flag.Arg(0), flag.Args()[1:]

	// Check all args for a special invalid path being passed along
	for _, arg := range args {
		if strings.Contains(arg, invalidPath) {
			fmt.Println("file not found")
			os.Exit(1)
		}
	}

	var out string
	var exitCode int
	var stayAlive bool
	switch filepath.Base(cmd) {
	case "echo":
		out = strings.Join(args, " ") + "\n"
		exitCode = 0
	case FakeQemuFailing:
		out = "qemu error message"
		exitCode = 1
	case FakeQemuSlow:
		stayAlive = true
		out = "welcome to qemu for turtles 🐢"
		exitCode = 0
	case "cp", "fvm", "zbi":
		// These utilities already had their args checked for invalid paths
		// above, so at this point it's a no-op
		exitCode = 0
	case "qemu-system-x86_64", "qemu-system-aarch64":
		stayAlive = true
		out = fmt.Sprintf("'%s'\n", successfulBootMarker)
		exitCode = 0
	case "ps":
		// This is kind of a mess. We can't rely on ps to reflect the killed
		// state of a child process that we don't wait for if we, the parent
		// process, are still alive, because it will become a zombie and still
		// show up in the process list. This hacks around that by filtering out
		// zombie processes and then patching in an appropriate exit code.

		// Request an explicit output format to normalize different flavors of `ps`:
		args = append([]string{"-ostate="}, args...)
		cmd := exec.Command(cmd, args...)
		psOut, err := cmd.Output()
		if err != nil {
			if cmderr, ok := err.(*exec.ExitError); ok {
				exitCode = cmderr.ExitCode()
			} else {
				t.Fatalf("Error proxying ps call: %s", err)
			}
		}
		if strings.HasPrefix(string(psOut), "Z") {
			exitCode = 1
		}
	case "symbolize":
		if err := fakeSymbolize(os.Stdin, os.Stdout); err != nil {
			t.Fatalf("failed during scan: %s", err)
		}

		exitCode = 0
	default:
		exitCode = 127
	}

	os.Stdout.WriteString(out)
	if stayAlive {
		// Simulate a long-running process
		time.Sleep(10 * time.Second)
	}

	os.Exit(exitCode)
}

// This tests the subprocess mocking itself
func TestProcessMocking(t *testing.T) {
	// Enable subprocess mocking
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	cmd := NewCommand("echo", "it", "works")
	out, err := cmd.Output()
	if err != nil {
		t.Fatalf("Error running mock command: %s", err)
	}
	if string(out) != "it works\n" {
		t.Fatalf("Mock command returned unexpected output: %q", out)
	}

	cmd = NewCommand("ps", "-p", strconv.Itoa(os.Getpid()))
	out, err = cmd.Output()
	if err != nil {
		t.Fatalf("Error running mock command: %s (%q)", err, out)
	}
}
