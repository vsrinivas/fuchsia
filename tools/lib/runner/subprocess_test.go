// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runner

import (
	"bytes"
	"context"
	"fmt"
	"strings"
	"testing"
	"time"
)

const (
	skipMessage      = "runner tests are meant for local testing only"
	defaultIOTimeout = 100 * time.Millisecond
)

func TestSubprocessRunner(t *testing.T) {
	t.Skip(skipMessage)

	t.Run("Run", func(t *testing.T) {
		t.Run("should execute a commmand", func(t *testing.T) {
			r := SubprocessRunner{}
			message := "Hello, World!"
			command := []string{"/bin/echo", message}

			stdout := new(bytes.Buffer)
			stderr := new(bytes.Buffer)
			if err := r.Run(context.Background(), command, stdout, stderr); err != nil {
				t.Fatalf("failed to run test. Got an error %v", err)
			}

			stdoutS := strings.TrimSpace(stdout.String())
			if stdoutS != "Hello, World!" {
				t.Fatalf("expected output '%s', but got %s", message, stdoutS)
			}

			stderrS := strings.TrimSpace(stderr.String())
			if stderrS != "" {
				t.Fatalf("expected empty stderr, but got %s", stderrS)
			}
		})

		t.Run("should error if the context Completes before the commmand", func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			cancel()
			r := SubprocessRunner{}
			command := []string{"/bin/sleep", "5"}

			stdout := new(bytes.Buffer)
			stderr := new(bytes.Buffer)

			err := r.Run(ctx, command, stdout, stderr)
			stdoutS := strings.TrimSpace(stdout.String())
			stderrS := strings.TrimSpace(stderr.String())

			if err == nil {
				t.Fatal(strings.Join([]string{
					"expected command to terminate early but it completed:",
					fmt.Sprintf("(stdout): %s\n", stdoutS),
					fmt.Sprintf("(stderr): %s\n", stderrS),
				}, "\n"))
			}
		})

		t.Run("should return an error if the command fails", func(t *testing.T) {
			r := SubprocessRunner{}
			command := []string{"not_a_command_12345"}

			stdout := new(bytes.Buffer)
			stderr := new(bytes.Buffer)

			err := r.Run(context.Background(), command, stdout, stderr)
			stdoutS := strings.TrimSpace(stdout.String())
			stderrS := strings.TrimSpace(stderr.String())

			if err == nil {
				t.Fatal(strings.Join([]string{
					"expected command to terminate early but it completed:",
					fmt.Sprintf("(stdout): %s\n", stdoutS),
					fmt.Sprintf("(stderr): %s\n", stderrS),
				}, "\n"))
			}
		})
	})

}
