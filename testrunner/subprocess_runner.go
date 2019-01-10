// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"context"
	"fmt"
	"io"
	"os/exec"
	"syscall"
	"time"
)

const (
	defaultTimeout = 10 * time.Second
)

// SubprocessRunner is a Runner that runs commands as local subprocesses.
type SubprocessRunner struct {
}

// Run executes the given command.
func (r *SubprocessRunner) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	cmd := exec.Cmd{
		Path:        command[0],
		Args:        command,
		Stdout:      stdout,
		Stderr:      stderr,
		SysProcAttr: &syscall.SysProcAttr{Setpgid: true},
	}

	if err := cmd.Start(); err != nil {
		return err
	}

	done := make(chan error)
	go func() {
		done <- cmd.Wait()
	}()

	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
		return fmt.Errorf("process was killed because the context completed")
	}
}
