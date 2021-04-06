// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package subprocess

import (
	"context"
	"io"
	"os"
	"os/exec"
	"syscall"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// Runner is a Runner that runs commands as local subprocesses.
type Runner struct {
	// Dir is the working directory of the subprocesses; if unspecified, that
	// of the current process will be used.
	Dir string

	// Env is the environment of the subprocess, following the usual convention of a list of
	// strings of the form "<environment variable name>=<value>".
	Env []string
}

// Run runs a command until completion or until a context is canceled, in
// which case the subprocess is killed so that no subprocesses it spun up are
// orphaned.
func (r *Runner) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	return r.RunWithStdin(ctx, command, stdout, stderr, os.Stdin)
}

// RunWithStdin operates identically to Run, but additionally pipes input to the
// process via stdin.
func (r *Runner) RunWithStdin(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer, stdin io.Reader) error {
	cmd := exec.Command(command[0], command[1:]...)
	cmd.Stdout = stdout
	cmd.Stderr = stderr
	cmd.Stdin = stdin
	cmd.Dir = r.Dir
	cmd.Env = r.Env
	// For some reason, adding the child to the same process group as the
	// current process disconnects it from stdin. So don't do it if we're
	// running a potentially interactive command that has access to stdin. Those
	// cases are less likely to involve chains of subprocesses anyway, so it's
	// not as important to be able to kill the entire chain.
	if stdin != os.Stdin {
		cmd.SysProcAttr = &syscall.SysProcAttr{
			// Set a process group ID so we can kill the entire group, meaning
			// the process and any of its children.
			Setpgid: true,
		}
	}
	if len(cmd.Env) > 0 {
		logger.Debugf(ctx, "environment of subprocess: %v", cmd.Env)
	}
	logger.Debugf(ctx, "starting: %v", cmd.Args)
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
		// Negating the process ID means interpret it as a process group ID, so
		// we kill the subprocess and all of its children.
		syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
		return ctx.Err()
	}
}
