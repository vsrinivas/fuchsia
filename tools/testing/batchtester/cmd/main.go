// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/testing/batchtester"
)

type flags struct {
	configPath string
}

func mainImpl(ctx context.Context, flags flags) error {
	if flags.configPath == "" {
		return fmt.Errorf("-config is required")
	}
	var config batchtester.Config
	if err := jsonutil.ReadFromFile(flags.configPath, &config); err != nil {
		return fmt.Errorf("failed to read config from %q: %w", flags.configPath, err)
	}
	return batchtester.Run(ctx, &config)
}

func main() {
	var flags flags
	flag.StringVar(&flags.configPath, "config", "", "Path to a batchtester config file")

	flag.Parse()

	l := logger.NewLogger(logger.InfoLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "batchtester ")
	l.SetFlags(log.Ltime | log.Lmicroseconds | log.Lshortfile)
	ctx := logger.WithLogger(context.Background(), l)
	ctx, cancel := signal.NotifyContext(ctx, syscall.SIGTERM, syscall.SIGINT)
	defer cancel()

	if err := mainImpl(ctx, flags); err != nil {
		logger.Fatalf(ctx, err.Error())
	}
}
