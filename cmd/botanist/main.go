// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"os"
	"syscall"

	"github.com/google/subcommands"

	"fuchsia.googlesource.com/tools/color"
	"fuchsia.googlesource.com/tools/command"
	"fuchsia.googlesource.com/tools/logger"
)

var (
	colors color.EnableColor
	level  logger.LogLevel
)

func init() {
	colors = color.ColorAuto
	level = logger.InfoLevel

	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(&ZedbootCommand{}, "")
	subcommands.Register(&QEMUCommand{}, "")
	subcommands.Register(&RunCommand{}, "")

	flag.Parse()

	log := logger.NewLogger(level, color.NewColor(colors), os.Stdout, os.Stderr)
	ctx := logger.WithLogger(context.Background(), log)
	ctx = command.CancelOnSignals(ctx, syscall.SIGTERM)
	os.Exit(int(subcommands.Execute(ctx)))
}
