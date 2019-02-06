// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fastboot

import (
	"context"
	"fmt"
	"log"
	"os/exec"
)

// Fastboot provides a subset of the functionality of the fastboot CLI tool.
type Fastboot struct {
	// ToolPath is the path to the fastboot command line tool.
	ToolPath string
}

func (f *Fastboot) exec(ctx context.Context, args ...string) ([]byte, error) {
	cmd := exec.CommandContext(ctx, f.ToolPath, args...)
	log.Printf("running:\n\tArgs: %s\n\tEnv: %s", cmd.Args, cmd.Env)
	out, err := cmd.Output()
	if ctx.Err() != nil {
		return nil, fmt.Errorf("context error: %v", ctx.Err())
	}
	if err != nil {
		// Put the standard error text into the returned error object
		if exitError, ok := err.(*exec.ExitError); ok && exitError.Stderr != nil {
			return out, fmt.Errorf(string(exitError.Stderr))
		}
	}
	return out, err
}

// Continue is equivalent to the command "fastboot continue".
func (f *Fastboot) Continue(ctx context.Context) ([]byte, error) {
	return f.exec(ctx, "continue")
}
