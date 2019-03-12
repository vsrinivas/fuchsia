// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package command_test

import (
	"context"
	"flag"
	"testing"
	"time"

	"fuchsia.googlesource.com/tools/command"
	"github.com/google/subcommands"
)

func TestCancelableExecute(t *testing.T) {
	tests := []struct {
		// The name of this test case
		name string

		// Whether to cancel the execution context early.
		cancelContextEarly bool

		// Whether the underlying subcommand is expected to finish
		expectToFinish bool
	}{
		{"when context is canceled early", true, false},
		{"when context is never canceled", false, true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tcmd := &TestCommand{}
			cmd := command.Cancelable(tcmd)
			ctx, cancel := context.WithCancel(context.Background())
			if tt.cancelContextEarly {
				cancel()
				cmd.Execute(ctx, flag.NewFlagSet("test", flag.ContinueOnError))
			} else {
				cmd.Execute(ctx, flag.NewFlagSet("test", flag.ContinueOnError))
				cancel()
			}

			if tcmd.DidFinish && !tt.expectToFinish {
				t.Errorf("wanted command to exit early but it finished")
			} else if !tcmd.DidFinish && tt.expectToFinish {
				t.Errorf("wanted command to finish but it exited early")
			}
		})
	}
}

// TestCancelableDelegation verifies that Cancelable() returns a subcommand.Command that
// delegates to the input subcommand.Command.
func TestCancelableDelegation(t *testing.T) {
	expectEq := func(t *testing.T, name, expected, actual string) {
		if expected != actual {
			t.Errorf("wanted %s to be %q but got %q", name, expected, actual)
		}
	}
	cmd := command.Cancelable(&TestCommand{
		name:     "test_name",
		usage:    "test_usage",
		synopsis: "test_synopsis",
	})
	expectEq(t, "Name", "test_name", cmd.Name())
	expectEq(t, "Usage", "test_usage", cmd.Usage())
	expectEq(t, "Synopsis", "test_synopsis", cmd.Synopsis())
}

type TestCommand struct {
	name, usage, synopsis string
	DidFinish             bool
}

func (cmd *TestCommand) Name() string             { return cmd.name }
func (cmd *TestCommand) Usage() string            { return cmd.usage }
func (cmd *TestCommand) Synopsis() string         { return cmd.synopsis }
func (cmd *TestCommand) SetFlags(f *flag.FlagSet) {}
func (cmd *TestCommand) Execute(ctx context.Context, f *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	time.Sleep(time.Millisecond)
	cmd.DidFinish = true
	return subcommands.ExitSuccess
}
