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
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/clock"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	// cleanupGracePeriod is the time period we allow the subprocess to complete in
	// after we send a SIGTERM.
	cleanupGracePeriod = 10 * time.Second
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
	pgidSet := false
	if stdin != os.Stdin {
		pgidSet = true
		cmd.SysProcAttr = &syscall.SysProcAttr{
			// Set a process group ID so we can kill the entire group, meaning
			// the process and any of its children.
			Setpgid: true,
		}
	}
	if len(cmd.Env) > 0 {
		logger.Debugf(ctx, "environment of subprocess: %v", cmd.Env)
	}

	// Ensure that the context still exists before running the subprocess.
	if ctx.Err() != nil {
		logger.Debugf(ctx, "context exited before starting subprocess")
		return ctx.Err()
	}

	logger.Debugf(ctx, "starting: %v", cmd.Args)
	if err := cmd.Start(); err != nil {
		return err
	}

	errs := make(chan error)

	go func() {
		errs <- cmd.Wait()
	}()

	select {
	case err := <-errs:
		// Process is done so no need to worry about cleanup. Just exit.
		return err
	case <-ctx.Done():
		if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
			logger.Debugf(ctx, "exited cmd %v with error: %s", cmd.Args, err)
		}

		// Wait up to `cleanupGracePeriod` for the subprocess to exit on its
		// own. If it takes too long we'll SIGKILL it.
		select {
		case <-errs:
			// The command has completed but it may still have child processes
			// running that we would like to clean up if possible. Sending a
			// SIGKILL to clean up the entire process group will only work if
			// the pgid is set.
			if pgidSet {
				killProcess(ctx, cmd, pgidSet)
			}
		case <-clock.After(ctx, cleanupGracePeriod):
			killProcess(ctx, cmd, pgidSet)
			// Wait for the subprocess to complete after killing it.
			<-errs
		}
		// Return the context error instead of the error returned by cmd.Wait()
		// to indicate to the caller that the command failed as a result of a
		// context cancellation; in this case the error returned by cmd.Wait()
		// will generally be more confusing than meaningful.
		return ctx.Err()
	}
}

// killProcess makes a best-effort attempt at killing the subprocess specified
// by `cmd`, along with all of its child processes if `pgidSet` is true.
func killProcess(ctx context.Context, cmd *exec.Cmd, pgidSet bool) {
	logger.Debugf(ctx, "killing process %d", cmd.Process.Pid)
	pgid := cmd.Process.Pid
	if pgidSet {
		// Negating the process ID means interpret it as a process group ID, so
		// we kill the subprocess and all of its children.
		pgid = -pgid
	}
	if err := syscall.Kill(pgid, syscall.SIGKILL); err != nil {
		logger.Debugf(ctx, "killed cmd %v with error: %s", cmd.Args, err)
	}
}
