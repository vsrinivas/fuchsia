// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"io/ioutil"

	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type SetCommand struct {
	staticSpecPath  string
	contextSpecPath string
}

func (*SetCommand) Name() string { return "set" }

func (*SetCommand) Synopsis() string { return "runs gn gen with args based on the input specs." }

func (*SetCommand) Usage() string {
	return `
fint set -context <path> -static <path>

flags:
`
}

func (c *SetCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&c.staticSpecPath, "static", "", "path to a Static .textproto")
	f.StringVar(&c.contextSpecPath, "context", "", "path to a Context .textproto")
}

func (c *SetCommand) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if c.staticSpecPath == "" || c.contextSpecPath == "" {
		logger.Errorf(ctx, "-static and -context flags are required")
		return subcommands.ExitUsageError
	}
	if err := c.run(ctx); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (c *SetCommand) run(ctx context.Context) error {
	logger.Debugf(ctx, "static spec path: %s", c.staticSpecPath)
	logger.Debugf(ctx, "context spec path: %s", c.contextSpecPath)

	bytes, err := ioutil.ReadFile(c.staticSpecPath)
	if err != nil {
		return err
	}

	if _, err := parseStatic(string(bytes)); err != nil {
		return err
	}

	return nil
}
