// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package subprocess

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/clock"
)

func TestRun(t *testing.T) {
	ctx := context.Background()

	t.Run("should execute a command", func(t *testing.T) {
		r := Runner{
			Env: []string{"FOO=bar"}, // Cover env var handling.
		}
		message := "Hello, World!"
		command := []string{"echo", message}

		stdout := new(bytes.Buffer)
		stderr := new(bytes.Buffer)
		if err := r.Run(ctx, command, RunOptions{Stdout: stdout, Stderr: stderr, Env: []string{"BAR=baz"}}); err != nil {
			t.Fatal(err)
		}

		stdoutS := strings.TrimSpace(stdout.String())
		if stdoutS != message {
			t.Fatalf("Expected output %q, but got %q", message, stdoutS)
		}

		stderrS := strings.TrimSpace(stderr.String())
		if stderrS != "" {
			t.Fatalf("Expected empty stderr, but got %q", stderrS)
		}
	})

	t.Run("should error if the context completes before the command", func(t *testing.T) {
		ctx, cancel := context.WithCancel(ctx)
		cancel()

		r := Runner{}
		command := []string{"sleep", "5"}
		err := r.Run(ctx, command, RunOptions{})
		if err == nil {
			t.Fatal("Expected sleep command to terminate early but it completed")
		} else if !errors.Is(err, ctx.Err()) {
			t.Fatalf("Expected Run() to return a context error after cancelation but got: %s", err)
		}
	})

	t.Run("should return an error if the command fails", func(t *testing.T) {
		r := Runner{}
		command := []string{"not_a_command_12345"}
		err := r.Run(ctx, command, RunOptions{})
		if err == nil {
			t.Fatalf("Expected invalid command to fail but it succeeded: %s", err)
		} else if !errors.Is(err, exec.ErrNotFound) {
			t.Fatalf("Expected Run() to return exec.ErrNotFound but got: %s", err)
		}
	})

	t.Run("should respect dir in options", func(t *testing.T) {
		script := writeScript(
			t,
			`#!/bin/bash
			pwd`,
		)
		r := Runner{Dir: t.TempDir()}
		dir := t.TempDir()
		var stdout bytes.Buffer
		if err := r.Run(ctx, []string{script}, RunOptions{Stdout: &stdout, Dir: dir}); err != nil {
			t.Fatal(err)
		}
		pwd := strings.TrimSpace(stdout.String())
		if pwd != dir {
			t.Errorf("Wrong dir: %s != %s", pwd, dir)
		}
	})

	t.Run("should set environment variables", func(t *testing.T) {
		for _, v := range []string{"PARENT_VAR", "OVERRIDDEN_PARENT_VAR"} {
			os.Setenv(v, "0")
			defer os.Unsetenv(v)
		}

		r := Runner{
			Env: []string{
				"OVERRIDDEN_PARENT_VAR=1",
				"RUNNER_VAR=1",
				"OVERRIDDEN_RUNNER_VAR=1",
			},
		}
		script := writeScript(
			t,
			`#!/bin/bash
			# Use null byte as a separator instead of newline since env var
			# values might contain newlines.
			env -0`,
		)
		var stdout bytes.Buffer
		if err := r.Run(ctx, []string{script}, RunOptions{
			Stdout: &stdout,
			Env: []string{
				"OVERRIDDEN_RUNNER_VAR=2",
				"RUN_VAR=2",
			},
		}); err != nil {
			t.Fatal(err)
		}
		nullByte := string([]byte{0})
		lines := strings.Split(strings.Trim(stdout.String(), nullByte), nullByte)
		vars := make(map[string]string)
		for _, line := range lines {
			k, v, ok := strings.Cut(line, "=")
			if !ok {
				t.Fatalf("Invalid line from script: %q", line)
			}
			vars[k] = v
		}
		expected := map[string]string{
			"PARENT_VAR":            "0",
			"OVERRIDDEN_PARENT_VAR": "1",
			"RUNNER_VAR":            "1",
			"OVERRIDDEN_RUNNER_VAR": "2",
			"RUN_VAR":               "2",
		}
		for k, v := range expected {
			if vars[k] != v {
				t.Errorf("Wrong value for env var %s: got %q, wanted %q", k, vars[k], v)
			}
		}
	})

	t.Run("should wait for command to finish after sending SIGTERM", func(t *testing.T) {
		// The script below will print `start` to signify that it's ready to handle
		// SIGTERMs and SIGINTs and then run cleanup() when it receives the signal.
		// By checking that `start` is printed before canceling the context and checking
		// that `finished` is printed after, we can assert that the cleanup() function
		// was run before the script exited.
		script := writeScript(
			t,
			`#!/bin/bash
			cleanup() {
				echo "finished"; exit 1
			}
			trap cleanup TERM INT
			echo "start"
			while true;do :; done`,
		)
		r := Runner{}
		command := []string{script}
		stdoutReader, stdout := io.Pipe()
		defer stdoutReader.Close()
		defer stdout.Close()
		ctx, cancel := context.WithCancel(ctx)
		defer cancel()
		go func() {
			buf := make([]byte, 20)
			// Wait for script to print `start` before calling cancel() to know
			// that it's ready to handle the SIGTERM.
			if _, err := stdoutReader.Read(buf); err != nil || !bytes.Contains(buf, []byte("start")) {
				t.Errorf("Failed to read `start` from stdout: %s, got: %s", err, string(buf))
			}
			cancel()
			// After sending the SIGTERM, check that the script ran cleanup() and
			// printed `finished`.
			buf = make([]byte, 20)
			if _, err := stdoutReader.Read(buf); err != nil || !bytes.Contains(buf, []byte("finished")) {
				t.Errorf("Failed to read `finished` from stdout: %s, got: %s", err, string(buf))
			}
		}()
		if err := r.Run(ctx, command, RunOptions{Stdout: stdout, Stderr: stdout}); err == nil {
			t.Errorf("Expected script to terminate early but it completed successfully")
		} else {
			if !errors.Is(err, context.Canceled) {
				t.Errorf("Expected Run() to return context.Canceled but got: %s", err)
			}
		}
	})

	t.Run("should kill command if it doesn't terminate after sending SIGTERM", func(t *testing.T) {
		if runtime.GOOS == "darwin" {
			// Setting the pgid doesn't work on Mac OS, so this test
			// will hang because it can't kill the sleep process.
			// TODO(fxbug.dev/86162): Enable if we can find a way to kill
			// the child processes.
			t.Skip("Skipping on Mac because setting pgid doesn't work")
		}
		// Random number between 10,000 and 20,000 seconds to make it more
		// likely to be unique to each test run, so leaked `sleep` processes
		// don't show up across test runs.
		rand.Seed(time.Now().UTC().UnixNano())
		sleepDuration := rand.Intn(10000) + 10000

		// The script below will print `start` to signify that it's ready to handle
		// SIGTERMs and SIGINTs and then run cleanup() when it receives the signal.
		// However, since it starts a `sleep` process, it actually waits for the process
		// to finish before entering cleanup(). This will test that the process group
		// gets killed if it can't clean up and exit in time. In this test, `finished`
		// should not be in the output because the process would have been killed before
		// it could run cleanup().
		script := writeScript(t, fmt.Sprintf(
			`#!/bin/bash
			cleanup() {
				echo "finished"; exit 1
			}
			trap cleanup TERM INT
			sleep %d`, sleepDuration),
		)

		stdoutReader, stdout := io.Pipe()
		defer stdoutReader.Close()
		defer stdout.Close()
		fakeClock := clock.NewFakeClock()
		ctx := clock.NewContext(ctx, fakeClock)
		ctx, cancel := context.WithCancel(ctx)
		defer cancel()
		go func() {
			// Wait for the script to start sleeping before canceling the context.
			for {
				cmd := exec.Command("pgrep", "-f", fmt.Sprintf("sleep %d", sleepDuration))
				// pgrep returns an exit code of 1 if it fails to
				// find anything, so ignore the error.
				output, _ := cmd.CombinedOutput()
				if len(output) != 0 {
					t.Logf("pgrep: %s", output)
					break
				}
				if ctx.Err() != nil {
					return
				}
			}
			cancel()
			// Wait for After() to be called before advancing the clock.
			<-fakeClock.AfterCalledChan()
			fakeClock.Advance(cleanupGracePeriod + time.Second)
			// The script should be killed before it reaches cleanup(), so `finished`
			// should NOT have been printed to stdout. No need to check the err from
			// Read() because we don't expect the script to print anything more to
			// stdout, so it should block until the deferred stdout.Close() gets
			// executed.
			buf := make([]byte, 20)
			stdoutReader.Read(buf)
			if bytes.Contains(buf, []byte("finished")) {
				t.Errorf("Expected script to be killed without doing cleanup")
			}
		}()

		r := Runner{}
		if err := r.Run(ctx, []string{script}, RunOptions{Stdout: stdout}); err == nil {
			t.Errorf("Expected script to terminate early but it completed successfully")
		} else {
			if !errors.Is(err, context.Canceled) {
				t.Errorf("Expected Run() to return context.Canceled but got: %s", err)
			}
		}
	})
}

func writeScript(t *testing.T, contents string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "script.sh")
	if err := os.WriteFile(path, []byte(contents), 0o755); err != nil {
		t.Fatal(err)
	}
	return path
}
