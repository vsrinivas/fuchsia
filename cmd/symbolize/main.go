// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"

	"fuchsia.googlesource.com/tools/color"
	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/symbolize"
)

var (
	llvmSymboPath string
	idsPath       string
	colorValue    string
)

func init() {
	flag.StringVar(&llvmSymboPath, "llvm-symbolizer", "llvm-symbolizer", "path to llvm-symbolizer")
	flag.StringVar(&idsPath, "ids", "", "path to ids.txt")
	flag.StringVar(&colorValue, "color", "auto", "can be `always`, `auto`, or `never`.")
}

func getColor() (color.Color, error) {
	ec := color.EnableColor(colorValue)
	switch ec {
	case color.ColorAlways, color.ColorNever, color.ColorAuto:
		return color.NewColor(ec), nil
	default:
		return nil, fmt.Errorf("invalid color option `%s`, possible values are `always`, `auto`, and `never`", colorValue)
	}
}

func main() {
	flag.Parse()
	painter, err := getColor()
	if err != nil {
		log.Fatal(err)
	}

	symbolizeLogger := logger.NewLogger(logger.WarningLevel, painter, os.Stderr, os.Stderr)
	ctx := logger.WithLogger(context.Background(), symbolizeLogger)

	symbolizer := symbolize.NewLLVMSymbolizer(llvmSymboPath)
	repo := symbolize.NewRepo()
	err = repo.AddSource(symbolize.NewIDsSource(idsPath))
	if err != nil {
		symbolizeLogger.Fatalf("%v", err)
	}
	demuxer := symbolize.NewDemuxer(repo, symbolizer)
	presenter := symbolize.NewBasicPresenter(os.Stdout, painter.Enabled())

	// Build the pipeline to start presenting.
	err = symbolizer.Start(ctx)
	if err != nil {
		symbolizeLogger.Fatalf("%v", err)
	}
	inputLines := symbolize.StartParsing(ctx, os.Stdin)
	outputLines := demuxer.Start(ctx, inputLines)
	presenter.Start(outputLines)
}
