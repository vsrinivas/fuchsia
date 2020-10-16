// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"regexp"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/integration/cmd/fint/proto"
)

type fakeSubprocessRunner struct {
	commandsRun [][]string
}

func (r *fakeSubprocessRunner) Run(_ context.Context, cmd []string, _, _ io.Writer) error {
	r.commandsRun = append(r.commandsRun, cmd)
	return nil
}

func TestRunGen(t *testing.T) {
	ctx := context.Background()
	runner := &fakeSubprocessRunner{}
	contextSpec := proto.Context{
		CheckoutDir: "/path/to/checkout",
		BuildDir:    "/path/to/out/default",
	}
	platform := "mac-x64"

	if err := runGen(ctx, runner, &contextSpec, platform); err != nil {
		t.Fatalf("Unexpected error from runGen: %v", err)
	}
	if len(runner.commandsRun) != 1 {
		t.Fatalf("Expected runGen to run one command, but it ran %d", len(runner.commandsRun))
	}

	cmd := runner.commandsRun[0]
	if len(cmd) < 3 {
		t.Fatalf("runGen ran wrong command: %v", cmd)
	}

	exe, subcommand, buildDir := cmd[0], cmd[1], cmd[2]
	// Intentionally flexible about the path within the checkout to the gn dir
	// in case it's every intentionally changed.
	expectedExePattern := regexp.MustCompile(
		fmt.Sprintf(`^%s(/\w+)+/%s/gn$`, contextSpec.CheckoutDir, platform),
	)
	if !expectedExePattern.MatchString(exe) {
		t.Errorf("runGen ran wrong GN executable: %s, expected a match of %s", exe, expectedExePattern.String())
	}
	if subcommand != "gen" {
		t.Errorf("Expected runGen to run `gn gen`, but got `gn %s`", subcommand)
	}
	if buildDir != contextSpec.BuildDir {
		t.Errorf("Expected runGen to use build dir from context (%s) but got %s", contextSpec.BuildDir, buildDir)
	}
}
