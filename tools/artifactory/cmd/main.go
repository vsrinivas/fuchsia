// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"os"
	"os/signal"
	"runtime/pprof"
	"syscall"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	"github.com/google/subcommands"
)

var (
	colors color.EnableColor
	level  logger.LogLevel
)

func init() {
	colors = color.ColorAuto
	level = logger.DebugLevel

	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(&upCommand{}, "")

	flag.Parse()
	log := logger.NewLogger(level, color.NewColor(colors), os.Stdout, os.Stderr, "artifactory ")
	ctx := logger.WithLogger(context.Background(), log)
	ctx, cancel := signal.NotifyContext(ctx, syscall.SIGTERM)
	defer cancel()

	go func() {
		<-ctx.Done()
		pprof.Lookup("goroutine").WriteTo(os.Stderr, 1)
	}()

	os.Exit(int(subcommands.Execute(ctx)))
}
