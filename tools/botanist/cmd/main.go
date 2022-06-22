// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"os"
	"os/signal"
	"syscall"

	"github.com/google/subcommands"

	"go.fuchsia.dev/fuchsia/tools/botanist"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/streams"
)

var (
	colors = color.ColorAuto
	level  = logger.InfoLevel
)

func init() {
	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(&RunCommand{}, "")

	flag.Parse()

	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer cancel()
	ctx = streams.ContextWithStdout(ctx, botanist.NewLockedWriter(os.Stdout))
	ctx = streams.ContextWithStderr(ctx, botanist.NewLockedWriter(os.Stderr))
	stdout, stderr, flush := botanist.NewStdioWriters(ctx)
	defer flush()
	l := logger.NewLogger(level, color.NewColor(colors), stdout, stderr, "botanist ")
	l.SetFlags(logger.Ltime | logger.Lmicroseconds | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)
	os.Exit(int(subcommands.Execute(ctx)))
}
