// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// Uses mDNS for Fuchsia device discovery.

import (
	"context"
	"flag"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	"github.com/google/subcommands"
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
	subcommands.Register(&listCmd{}, "")
	subcommands.Register(&resolveCmd{}, "")

	flag.Parse()

	log.SetFlags(log.Lshortfile)

	l := logger.NewLogger(level, color.NewColor(colors), os.Stdout, os.Stderr, "device-finder ")
	l.SetFlags(logger.Lshortfile)
	ctx := logger.WithLogger(context.Background(), l)

	os.Exit(int(subcommands.Execute(ctx)))
}
