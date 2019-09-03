// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package command

import (
	"context"
	"flag"
	"log"

	"github.com/google/subcommands"
)

// Disposer is an object that performs tear down. This is used by Cancelable to gracefully
// terminate a delegate Command before exiting.
type Disposer interface {
	Dispose()
}

// Cancelable wraps a subcommands.Command so that it is canceled if its input execution
// context emits a Done event before execution is finished. If the given Command
// implements the Disposer interface, Dispose is called before the program exits.
func Cancelable(sub subcommands.Command) subcommands.Command {
	return &cancelable{sub}
}

// cancelable wraps a subcommands.Command so that it is canceled if the input execution
// context emits a Done event before execution is finished. cancelable "masquerades" as
// the underlying Command. Example Registration:
//
//   subcommands.Register(command.Cancelable(&OtherSubcommand{}))
type cancelable struct {
	sub subcommands.Command
}

// Name forwards to the underlying Command.
func (cmd *cancelable) Name() string {
	return cmd.sub.Name()
}

// Usage forwards to the underlying Command.
func (cmd *cancelable) Usage() string {
	return cmd.sub.Usage()
}

// Synopsis forwards to the underlying Command.
func (cmd *cancelable) Synopsis() string {
	return cmd.sub.Synopsis()
}

// SetFlags forwards to the underlying Command.
func (cmd *cancelable) SetFlags(f *flag.FlagSet) {
	cmd.sub.SetFlags(f)
}

// Execute runs the underlying Command in a goroutine. If the input context is canceled
// before execution finishes, execution is canceled and the context's error is logged.
func (cmd *cancelable) Execute(ctx context.Context, f *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	status := make(chan subcommands.ExitStatus)
	go func() {
		status <- cmd.sub.Execute(ctx, f, args...)
	}()
	select {
	case <-ctx.Done():
		if d, ok := cmd.sub.(Disposer); ok {
			d.Dispose()
		}
		log.Println(ctx.Err())
		return subcommands.ExitFailure
	case s := <-status:
		close(status)
		return s
	}
}
