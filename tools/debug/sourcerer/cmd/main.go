// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"sort"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

var (
	colors color.EnableColor
	level  logger.LogLevel
)

func usage() {
	fmt.Printf(`srcls [binary]

Prints each DWARF-determined source of a given binary.
`)
}

func init() {
	colors = color.ColorAuto
	level = logger.InfoLevel

	flag.Var(&colors, "color", "can be never, auto, always")
	flag.Var(&level, "level", "can be fatal, error, warning, info, debug or trace")

	flag.Usage = usage
}

func execute() error {
	if flag.NArg() != 1 {
		return fmt.Errorf("binary expected as a single positional argument")
	}
	bin := flag.Arg(0)
	srcs, err := elflib.ListSources(bin)
	if err != nil {
		return err
	}
	sort.Strings(srcs)
	for _, src := range srcs {
		fmt.Println(src)
	}
	return nil
}

func main() {
	flag.Parse()

	log := logger.NewLogger(level, color.NewColor(colors), os.Stdout, os.Stderr, "")
	ctx := logger.WithLogger(context.Background(), log)
	if err := execute(); err != nil {
		logger.Fatalf(ctx, "%v", err)
	}
}
