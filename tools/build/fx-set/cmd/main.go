// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"syscall"

	flag "github.com/spf13/pflag"
	"go.fuchsia.dev/fuchsia/tools/integration/fint"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/command"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
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

	var staticSpec *fintpb.Static
	if args.fintParamsPath == "" {
		staticSpec, err = constructStaticSpec(checkoutDir, args)
		if err != nil {
			return err
		}
	} else {
		path := args.fintParamsPath
		if !filepath.IsAbs(path) {
			path = filepath.Join(checkoutDir, path)
		}
		staticSpec, err = fint.ReadStatic(path)
		if err != nil {
			return err
		}
	}

	contextSpec := &fintpb.Context{
		CheckoutDir: checkoutDir,
		// TODO(olivernewman): Implement support for --auto-dir and --build-dir
		// flags to make this configurable.
		BuildDir: filepath.Join(checkoutDir, "out", "default"),
	}

	_, err = fint.Set(ctx, staticSpec, contextSpec)
	return err
}

type setArgs struct {
	verbose        bool
	fintParamsPath string

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
	flagSet.StringVar(&cmd.fintParamsPath, "fint-params-path", "", "")
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

	// If a fint params file was specified then no other arguments are required,
	// so no need to validate them.
	if cmd.fintParamsPath != "" {
		return cmd, nil
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

func constructStaticSpec(checkoutDir string, args *setArgs) (*fintpb.Static, error) {
	productPath, err := findGNIFile(checkoutDir, "products", args.product)
	if err != nil {
		return nil, fmt.Errorf("no such product %q", args.product)
	}
	boardPath, err := findGNIFile(checkoutDir, "boards", args.board)
	if err != nil {
		return nil, fmt.Errorf("no such board: %q", args.board)
	}

	optimize := fintpb.Static_DEBUG
	if args.isRelease {
		optimize = fintpb.Static_RELEASE
	}

	return &fintpb.Static{
		Board:            boardPath,
		Product:          productPath,
		Optimize:         optimize,
		BasePackages:     args.basePackages,
		CachePackages:    args.cachePackages,
		UniversePackages: args.universePackages,
		HostLabels:       args.hostLabels,
		Variants:         args.variants,
		GnArgs:           args.gnArgs,
	}, nil
}

// findGNIFile returns the relative path to a board or product file in a
// checkout, given a basename. It checks the root of the checkout as well as
// each vendor/* directory for a file matching "<dirname>/<basename>.gni", e.g.
// "boards/core.gni".
func findGNIFile(checkoutDir, dirname, basename string) (string, error) {
	dirs, err := filepath.Glob(filepath.Join(checkoutDir, "vendor", "*", dirname))
	if err != nil {
		return "", err
	}
	dirs = append(dirs, filepath.Join(checkoutDir, dirname))

	for _, dir := range dirs {
		path := filepath.Join(dir, fmt.Sprintf("%s.gni", basename))
		exists, err := osmisc.FileExists(path)
		if err != nil {
			return "", err
		}
		if exists {
			return filepath.Rel(checkoutDir, path)
		}
	}

	return "", fmt.Errorf("no such file %s.gni", basename)
}
