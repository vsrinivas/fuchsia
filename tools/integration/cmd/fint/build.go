// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"

	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/tools/integration/fint"
)

type BuildCommand struct {
	BaseCommand
}

func (*BuildCommand) Name() string { return "build" }

func (*BuildCommand) Synopsis() string {
	return "runs ninja with targets based on the input specs."
}

func (*BuildCommand) Usage() string {
	return `fint build -static <path> [-context <path>]

flags:
`
}

func (c *BuildCommand) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	return c.execute(ctx, func(ctx context.Context) error {
		staticSpec, contextSpec, err := c.loadSpecs()
		if err != nil {
			return err
		}

		return fint.Build(ctx, staticSpec, contextSpec)
	})
}
