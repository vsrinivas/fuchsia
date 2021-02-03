// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"os"
	"strings"
	"syscall"

	flag "github.com/spf13/pflag"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/command"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	// fx ensures that this env var is set.
	checkoutDirEnvVar = "FUCHSIA_DIR"
)

func main() {
	ctx := command.CancelOnSignals(context.Background(), syscall.SIGTERM, syscall.SIGINT)

	l := logger.NewLogger(logger.ErrorLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "")
	// Don't include timestamps or other metadata in logs, since this tool is
	// only intended to be run on developer workstations.
	l.SetFlags(0)
	ctx = logger.WithLogger(ctx, l)

	if err := mainImpl(ctx); err != nil {
		if ctx.Err() == nil {
			logger.Errorf(ctx, err.Error())
		}
		os.Exit(1)
	}
}

func mainImpl(ctx context.Context) error {
	checkoutDir := os.Getenv(checkoutDirEnvVar)
	if checkoutDir == "" {
		return fmt.Errorf("%s env var must be set", checkoutDirEnvVar)
	}

	args, err := parseArgs(os.Args[1:])
	if err != nil {
		return err
	}

	if args.verbose {
		if l := logger.LoggerFromContext(ctx); l != nil {
			l.LoggerLevel = logger.DebugLevel
		}
	}

	return nil
}

type setArgs struct {
	verbose bool

	// Flags passed to GN.
	board            string
	product          string
	isRelease        bool
	universePackages []string
	basePackages     []string
	cachePackages    []string
	hostLabels       []string
	variants         []string
	gnArgs           []string
}

func parseArgs(args []string) (*setArgs, error) {
	cmd := &setArgs{}

	flagSet := flag.NewFlagSet("fx set", flag.ExitOnError)

	// Help strings don't matter because `fx set -h` uses the help text from
	// //tools/devshell/set, which should be kept up to date with these flags.
	flagSet.BoolVar(&cmd.verbose, "verbose", false, "")
	flagSet.BoolVar(&cmd.isRelease, "release", false, "")
	flagSet.StringSliceVar(&cmd.universePackages, "with", []string{}, "")
	flagSet.StringSliceVar(&cmd.basePackages, "with-base", []string{}, "")
	flagSet.StringSliceVar(&cmd.cachePackages, "with-cache", []string{}, "")
	flagSet.StringSliceVar(&cmd.hostLabels, "with-host", []string{}, "")
	flagSet.StringSliceVar(&cmd.variants, "variant", []string{}, "")
	// Unlike StringSliceVar, StringArrayVar doesn't split flag values at
	// commas. Commas are syntactically significant in GN, so they should be
	// preserved rather than interpreting them as value separators.
	flagSet.StringArrayVar(&cmd.gnArgs, "args", []string{}, "")

	if err := flagSet.Parse(args); err != nil {
		return nil, err
	}

	if flagSet.NArg() == 0 {
		return nil, fmt.Errorf("missing a PRODUCT.BOARD argument")
	} else if flagSet.NArg() > 1 {
		return nil, fmt.Errorf("only one positional PRODUCT.BOARD argument allowed")
	}

	productDotBoard := flagSet.Arg(0)
	productAndBoard := strings.Split(productDotBoard, ".")
	if len(productAndBoard) != 2 {
		return nil, fmt.Errorf("unable to parse PRODUCT.BOARD: %q", productDotBoard)
	}
	cmd.product, cmd.board = productAndBoard[0], productAndBoard[1]

	return cmd, nil
}
