// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package subprocess

import (
	"bytes"
	"context"
	"errors"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/clock"
)

func TestRun(t *testing.T) {
	t.Run("Run", func(t *testing.T) {
		t.Run("should execute a command", func(t *testing.T) {
			r := Runner{
				Env: []string{"FOO=bar"}, // Cover env var handling.
			}
			message := "Hello, World!"
			command := []string{"echo", message}

			stdout := new(bytes.Buffer)
			stderr := new(bytes.Buffer)
			if err := r.Run(context.Background(), command, stdout, stderr); err != nil {
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
			ctx, cancel := context.WithCancel(context.Background())
			cancel()

			r := Runner{}
			command := []string{"sleep", "5"}
			err := r.Run(ctx, command, nil, nil)
			if err == nil {
				t.Fatal("Expected sleep command to terminate early but it completed")
			} else if !errors.Is(err, ctx.Err()) {
				t.Fatalf("Expected Run() to return a context error after cancelation but got: %s", err)
			}
		})

		t.Run("should return an error if the command fails", func(t *testing.T) {
			r := Runner{}
			command := []string{"not_a_command_12345"}
			err := r.Run(context.Background(), command, nil, nil)
			if err == nil {
				t.Fatalf("Expected invalid command to fail but it succeeded: %s", err)
			} else if !errors.Is(err, exec.ErrNotFound) {
				t.Fatalf("Expected Run() to return exec.ErrNotFound but got: %s", err)
			}
		})

		t.Run("should wait for command to finish after sending SIGTERM", func(t *testing.T) {
			tmpDir := t.TempDir()
			script := filepath.Join(tmpDir, "script")
			if err := ioutil.WriteFile(script, []byte(
				`#!/bin/bash
				cleanup() {
					echo "finished"; exit 1
				}
				trap cleanup TERM INT
				echo "start"
				while true;do :; done`,
			), os.ModePerm); err != nil {
				t.Fatalf("failed to write script: %s", err)
			}
			r := Runner{}
			command := []string{script}
			stdout := new(bytes.Buffer)
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			go func() {
				for ctx.Err() == nil {
					if strings.Contains(stdout.String(), "start") {
						break
					}
				}
				cancel()
			}()
			err := r.Run(ctx, command, stdout, stdout)
			if err == nil {
				t.Errorf("Expected script to terminate early but it completed successfully")
			} else {
				var exitError *exec.ExitError
				if !errors.As(err, &exitError) {
					t.Errorf("Expected Run() to return exec.ExitError but got: %s", err)
				}
			}
			if !strings.Contains(stdout.String(), "finished") {
				t.Fatalf("Expected script to print 'finished' before exiting; got: %s", stdout.String())
			}
		})

		t.Run("should kill command if it doesn't terminate after sending SIGTERM", func(t *testing.T) {
			tmpDir := t.TempDir()
			script := filepath.Join(tmpDir, "script")
			if err := ioutil.WriteFile(script, []byte(
				`#!/bin/bash
                                cleanup() {
					echo "finished"; exit 1
                                }
                                trap cleanup TERM INT
                                echo "start"
				sleep 10000`,
			), os.ModePerm); err != nil {
				t.Fatalf("failed to write script: %s", err)
			}
			r := Runner{}
			command := []string{script}
			stdout := new(bytes.Buffer)
			fakeClock := clock.NewFakeClock()
			ctx := clock.NewContext(context.Background(), fakeClock)
			ctx, cancel := context.WithCancel(ctx)
			defer cancel()
			go func() {
				for ctx.Err() == nil {
					if strings.Contains(stdout.String(), "start") {
						break
					}
				}
				cancel()
				// Wait for After() to be called before advancing the clock.
				<-fakeClock.AfterCalledChan()
				fakeClock.Advance(finishTimeout + time.Second)
			}()
			err := r.RunWithStdin(ctx, command, stdout, stdout, nil)
			if err == nil {
				t.Errorf("Expected script to terminate early but it completed successfully")
			} else {
				var exitError *exec.ExitError
				if !errors.As(err, &exitError) {
					t.Errorf("Expected Run() to return exec.ExitError but got: %s", err)
				}
			}
			if strings.Contains(stdout.String(), "finished") {
				t.Fatal("Expected script to be killed without doing cleanup")
			}
		})
	})
}
