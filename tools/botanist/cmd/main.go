// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"log"
	"os"
	"syscall"

	"github.com/google/subcommands"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/command"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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
	const logFlags = log.Ltime | log.Lmicroseconds | log.Lshortfile

	// Our mDNS library doesn't use the logger library.
	log.SetFlags(logFlags)

	log := logger.NewLogger(level, color.NewColor(colors), os.Stdout, os.Stderr, "botanist ")
	log.SetFlags(logFlags)
	ctx := logger.WithLogger(context.Background(), log)

	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(&ZedbootCommand{}, "")
	subcommands.Register(&QEMUCommand{}, "")
	subcommands.Register(&RunCommand{}, "")

	flag.Parse()

	ctx = command.CancelOnSignals(ctx, syscall.SIGTERM)
	os.Exit(int(subcommands.Execute(ctx)))
}
