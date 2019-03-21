// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"context"
	"io"
	"os/exec"

	"fuchsia.googlesource.com/tools/botanist"
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

// Run executes the given command.
func (r *SubprocessRunner) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	cmd := exec.Cmd{
		Path:   command[0],
		Args:   command,
		Stdout: stdout,
		Stderr: stderr,
		Dir:    r.WD,
		Env:    r.Env,
	}
	return botanist.Run(ctx, cmd)
}
