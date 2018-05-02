// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"log"
	"os"

	"fuchsia.googlesource.com/tools/symbolize"
)

func defaultColor() bool {
	// TODO(jakehehrlich): TC-83: Use a proper terminal info library to get this value
	return os.Getenv("TERM") != "dumb"
}

var (
	llvmSymboPath string
	idsPath       string
	color         bool
)

func init() {
	flag.StringVar(&llvmSymboPath, "llvm-symbolizer", "llvm-symbolizer", "path to llvm-symbolizer")
	flag.StringVar(&idsPath, "ids", "", "path to ids.txt")
	flag.BoolVar(&color, "color", defaultColor(), "use -color to enable color output and -color=0 to disable")
}

func main() {
	flag.Parse()

	symbolizer := symbolize.NewLLVMSymbolizer(llvmSymboPath)
	repo := symbolize.NewRepo()
	err := repo.AddObjectsFromIdsFile(idsPath)
	if err != nil {
		log.Fatal(err)
	}
	demuxer := symbolize.NewDemuxer(repo, symbolizer)
	presenter := symbolize.NewBasicPresenter(os.Stdout, color)

	ctx := context.Background()

	// Build the pipeline to start presenting.
	err = symbolizer.Start(ctx)
	if err != nil {
		log.Fatal(err)
	}
	inputLines := symbolize.StartParsing(ctx, os.Stdin)
	outputLines := demuxer.Start(ctx, inputLines)
	presenter.Start(outputLines)
}
