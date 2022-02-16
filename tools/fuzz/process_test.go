// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"flag"
	"fmt"
	"io"
	"math/rand"
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
	// For ps, pass through directly
	if command == "ps" {
		return exec.Command(command, args...)
	}

	// Call ourself as the subprocess so we can mock behavior as appropriate
	argv := []string{"-logtostderr", "-test.run=TestDoProcessMock", "--", command}
	argv = append(argv, args...)

	cmd := exec.Command(os.Args[0], argv...)
	cmd.Env = os.Environ()
	cmd.Env = append(cmd.Env, "MOCK_PROCESS=yes")
	// Ensure the subprocess CPRNG is seeded uniquely (but deterministically)
	cmd.Env = append(cmd.Env, fmt.Sprintf("RAND_SEED=%d", rand.Int63()))

	return cmd
}

// This is the actual implementation of mocked subprocesses.
// The name is a little confusing because it has to have the Test prefix
func TestDoProcessMock(t *testing.T) {
	if os.Getenv("MOCK_PROCESS") != "yes" {
		t.Skip("Not in a subprocess")
	}
	seed, err := strconv.ParseInt(os.Getenv("RAND_SEED"), 10, 64)
	if err != nil {
		t.Fatalf("Invalid CPRNG seed: %s", err)
	}
	rand.Seed(seed)

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
	// The following utilities already had their args checked for invalid paths
	// above, so at this point we just need to touch an expected output file.
	// The contents are randomized to allow for simple change detection.
	case "cp":
		touchRandomFile(t, args[len(args)-1])
		exitCode = 0
	case "fvm":
		touchRandomFile(t, args[0])
		exitCode = 0
	case "zbi":
		found := false
		for j, arg := range args[:len(args)-1] {
			if arg == "-o" {
				touchRandomFile(t, args[j+1])
				found = true
			}
		}
		if !found {
			t.Fatalf("No output specified in zbi command args: %s", args)
		}
		exitCode = 0
	case "qemu-system-x86_64", "qemu-system-aarch64":
		stayAlive = true
		var logFile string
		// Check for a logfile specified in a serial parameter
		for j, arg := range args[:len(args)-1] {
			if arg != "-serial" {
				continue
			}
			parts := strings.SplitN(args[j+1], ":", 2)
			if parts[0] == "file" {
				logFile = parts[1]
			}
		}

		logWriter := os.Stdout
		if logFile != "" {
			outFile, err := os.Create(logFile)
			if err != nil {
				t.Fatalf("error creating qemu log file: %s", err)
			}
			defer outFile.Close()
			logWriter = outFile
		}
		io.WriteString(logWriter, "early boot\n")
		io.WriteString(logWriter, successfulBootMarker+"\n")

		// Output >100KB of additional data to ensure we handle large logs correctly
		filler := strings.Repeat("data", 1024/4)
		for j := 0; j < 100; j++ {
			logLine := fmt.Sprintf("%d: %s", j, filler)
			io.WriteString(logWriter, logLine)
		}

		// Write a final success marker
		io.WriteString(logWriter, lateBootMessage+"\n")

		exitCode = 0
	case "symbolizer":
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
