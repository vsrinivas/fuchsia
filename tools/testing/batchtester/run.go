// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package batchtester

import (
	"context"
	"errors"
	"fmt"
	"os/exec"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/clock"
	"go.fuchsia.dev/fuchsia/tools/lib/streams"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

// TODO(olivernewman): Make this configurable on a per-test basis.
const perTestTimeout = 10 * time.Minute

func Run(ctx context.Context, config *Config) error {
	var runner subprocess.Runner
	var failures int
	for _, test := range config.Tests {
		// TODO(olivernewman): Stream results to ResultDB.
		r, err := runTest(ctx, &runner, test)
		if err != nil {
			return err
		}
		if r.Status != Pass {
			failures++
		}
	}
	if failures > 0 {
		return fmt.Errorf("%d of %d test(s) failed", failures, len(config.Tests))
	}
	return nil
}

func runTest(ctx context.Context, runner *subprocess.Runner, test Test) (TestResult, error) {
	testCtx, cancel := context.WithTimeout(ctx, perTestTimeout)
	defer cancel()

	options := subprocess.RunOptions{
		Stdout: streams.Stdout(ctx),
		Stderr: streams.Stderr(ctx),
		Dir:    test.Execroot,
	}
	startTime := clock.Now(ctx)
	// TODO(olivernewman): Sandbox using nsjail.
	err := runner.Run(testCtx, []string{test.Executable}, options)
	endTime := clock.Now(ctx)

	result := TestResult{
		Name:     test.Name,
		Duration: endTime.Sub(startTime),
	}

	if err == nil {
		result.Status = Pass
	} else if errExit := (*exec.ExitError)(nil); errors.As(err, &errExit) {
		result.Status = Fail
	} else if errors.Is(err, context.DeadlineExceeded) && ctx.Err() == nil {
		// Only consider a test to have timed out if the test's context timed
		// out without the parent context timing out.
		result.Status = Abort
	} else {
		// Unrecognized error.
		return TestResult{}, err
	}

	return result, nil
}
