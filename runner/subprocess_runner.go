// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runner

import (
	"context"
	"io"
	"os/exec"
	"syscall"
)

// SubprocessRunner is a Runner that runs commands as local subprocesses.
type SubprocessRunner struct {
	// WD is the working directory of the subprocesses; if unspecified, that
	// of the current process will be used.
	WD string

	// Env is the environment of the subprocess, following the usual convention of a list of
	// strings of the form "<environment variable name>=<value>".
	Env []string
}

// Run runs a command until completion or until a context is canceled, in
// which case the subprocess is killed so that no subprocesses it spun up are
// orphaned.
func (r *SubprocessRunner) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	cmd := exec.Cmd{
		Path:        command[0],
		Args:        command,
		Stdout:      stdout,
		Stderr:      stderr,
		Dir:         r.WD,
		Env:         r.Env,
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
		return ctx.Err()
	}
}
