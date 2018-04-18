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

var (
	llvmSymboPath string
	idsPath       string
)

func init() {
	flag.StringVar(&llvmSymboPath, "llvm-symbolizer", "llvm-symbolizer", "path to llvm-symbolizer")
	flag.StringVar(&idsPath, "ids", "", "path to ids.txt")
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
	presenter := symbolize.NewBasicPresenter(os.Stdout)

	ctx := context.Background()

	// Build the pipeline to start presenting.
	err = symbolizer.Start(ctx)
	if err != nil {
		log.Fatal(err)
	}
	inputLines := symbolize.StartParsing(os.Stdin, ctx)
	outputLines := demuxer.Start(inputLines, ctx)
	presenter.Start(outputLines)
}
