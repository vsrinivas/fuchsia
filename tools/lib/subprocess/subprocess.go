// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package subprocess

import (
	"context"
	"io"
	"os"
	"os/exec"
	"sync"
	"syscall"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/clock"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	// finishTimeout is the timeout we allow the subprocess to complete in
	// after we send a SIGTERM.
	finishTimeout = 10 * time.Second
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

	// Spin off handler to exit subprocesses cleanly via SIGTERM.
	processDone := make(chan struct{})
	var processMu sync.Mutex
	go func() {
		select {
		case <-processDone:
		case <-ctx.Done():
			// We need to check if the process is nil because it won't exist if
			// it has been SIGKILL'd already.
			processMu.Lock()
			defer processMu.Unlock()
			if cmd.Process != nil {
				if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
					logger.Debugf(ctx, "exited cmd with error %s", err)
				}
				// If the subprocess doesn't complete within the finishTimeout,
				// send a SIGKILL to force it to exit.
				go func() {
					select {
					case <-processDone:
					case <-clock.After(ctx, finishTimeout):
						logger.Debugf(ctx, "killing process %d", cmd.Process.Pid)
						pgid := cmd.Process.Pid
						if pgidSet {
							// Negating the process ID means interpret it as a process group ID, so
							// we kill the subprocess and all of its children.
							pgid = -pgid
						}
						if err := syscall.Kill(pgid, syscall.SIGKILL); err != nil {
							logger.Debugf(ctx, "killed cmd with error %s", err)
						}
					}
				}()
			}
		}
	}()

	// Ensure that the context still exists before running the subprocess.
	if ctx.Err() != nil {
		logger.Debugf(ctx, "context exited before starting subprocess")
		return ctx.Err()
	}

	// We need to make this a critical section because running Start changes
	// cmd.Process, which we attempt to access in the goroutine above. Not locking
	// causes a data race.
	logger.Debugf(ctx, "starting: %v", cmd.Args)
	processMu.Lock()
	err := cmd.Start()
	processMu.Unlock()
	if err != nil {
		close(processDone)
		return err
	}
	// Since we wait for the command to complete even if we send a SIGTERM when the
	// context is canceled, it is up to the underlying command to exit with the
	// proper exit code after handling a SIGTERM.
	err = cmd.Wait()
	close(processDone)
	if ctx.Err() != nil && pgidSet {
		// If the subprocess was terminated early and completed without terminating
		// its child processes, kill any remaining processes in the group.
		logger.Debugf(ctx, "killing process %d", cmd.Process.Pid)
		if err := syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL); err != nil {
			logger.Debugf(ctx, "killed cmd with error %s", err)
		}
	}
	return err
}
