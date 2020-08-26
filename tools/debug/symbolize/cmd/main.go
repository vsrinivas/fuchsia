// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/debug/symbolize"
	"go.fuchsia.dev/fuchsia/tools/lib/cache"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type argList []string

func (a *argList) String() string {
	return fmt.Sprintf("%s", []string(*a))
}

func (a *argList) Set(value string) error {
	*a = append(*a, value)
	return nil
}

const (
	symbolCacheSize = 100
	defaultTimeout  = 5 * time.Second
)

var (
	symbolServers   argList
	symbolCache     string
	symbolIndex     string
	buildIDDirPaths argList
	colors          color.EnableColor
	jsonOutput      string
	idsPaths        argList
	// TODO(jakehehrlich): Make idsRel always true and remove this flag.
	idsRel                   bool
	level                    logger.LogLevel
	llvmSymboPath            string
	llvmSymboRestartInterval uint
	cloudFetchTimeout        time.Duration
)

func init() {
	colors = color.ColorAuto
	level = logger.InfoLevel

	defaultSymbolIndex := ""
	if homeDir, err := os.UserHomeDir(); err == nil {
		defaultSymbolIndex = filepath.Join(homeDir, ".fuchsia", "debug", "symbol-index")
	}

	flag.Var(&symbolServers, "symbol-server", "a GCS URL or bucket name that contains debug binaries indexed by build ID")
	flag.StringVar(&symbolCache, "symbol-cache", "", "path to directory to store cached debug binaries in")
	flag.StringVar(&symbolIndex, "symbol-index", defaultSymbolIndex, "path to the symbol-index file")
	flag.Var(&buildIDDirPaths, "build-id-dir", "path to .build-id directory")
	flag.StringVar(&llvmSymboPath, "llvm-symbolizer", "llvm-symbolizer", "path to llvm-symbolizer")
	flag.Var(&idsPaths, "ids", "(deprecated) alias for -ids-txt")
	flag.Var(&idsPaths, "ids-txt", "path to ids.txt")
	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
	flag.StringVar(&jsonOutput, "json-output", "", "outputs trigger information to the specified file")
	flag.BoolVar(&idsRel, "ids-rel", false, "tells the symbolizer to always use ids.txt relative paths")
	flag.UintVar(&llvmSymboRestartInterval, "llvm-symbolizer-restart-interval", 15,
		"How many queries to make to the llvm-symbolizer tool before restarting it. 0 means never restart it. Use to control memory usage. See fxbug.dev/42018.")
	flag.DurationVar(&cloudFetchTimeout, "symbol-server-timeout", defaultTimeout, "Symbol server timeout for fetching an object from gs")
}

func main() {
	// Parse flags and setup helpers
	flag.Parse()

	// Setup logger and context
	painter := color.NewColor(colors)
	log := logger.NewLogger(level, painter, os.Stdout, os.Stderr, "")
	ctx := logger.WithLogger(context.Background(), log)

	symbolizer := symbolize.NewLLVMSymbolizer(llvmSymboPath, llvmSymboRestartInterval)
	var repo symbolize.CompositeRepo
	for _, dir := range buildIDDirPaths {
		repo.AddRepo(symbolize.NewBuildIDRepo(dir))
	}
	for _, idsPath := range idsPaths {
		repo.AddRepo(symbolize.NewIDsTxtRepo(idsPath, idsRel))
	}

	// Setup symbol index.
	if symbolIndex != "" {
		if _, err := os.Stat(symbolIndex); !os.IsNotExist(err) {
			file, err := os.Open(symbolIndex)
			if err != nil {
				log.Fatalf("failed to open: %s", err)
			}
			defer file.Close()

			index, err := symbolize.LoadIndex(file)
			if err != nil {
				log.Fatalf("failed to load the symbol-index: %s", err)
			}
			for _, entry := range index {
				if fi, err := os.Stat(entry.SymbolPath); !os.IsNotExist(err) {
					if fi.IsDir() {
						repo.AddRepo(symbolize.NewBuildIDRepo(entry.SymbolPath))
					} else {
						repo.AddRepo(symbolize.NewIDsTxtRepo(entry.SymbolPath, idsRel))
					}
				}
			}
		}
	}

	// Setup symbol cache.
	var filecache *cache.FileCache
	if len(symbolServers) > 0 {
		if symbolCache == "" {
			log.Fatalf("-symbol-cache must be set if a symbol server is used")
		}
		var err error
		if filecache, err = cache.GetFileCache(symbolCache, symbolCacheSize); err != nil {
			log.Fatalf("%v\n", err)
		}
	}
	for _, symbolServer := range symbolServers {
		// TODO(atyfto): Remove when all consumers are passing GCS URLs.
		if !strings.HasPrefix(symbolServer, "gs://") {
			symbolServer = "gs://" + symbolServer
		}
		cloudRepo, err := symbolize.NewCloudRepo(ctx, symbolServer, filecache)
		if err != nil {
			log.Fatalf("%v\n", err)
		}
		// Set a timeout for fetching an object from gs, defaults to 5 seconds
		cloudRepo.SetTimeout(cloudFetchTimeout)
		repo.AddRepo(cloudRepo)
	}

	// Construct the nodes of the pipeline
	demuxer := symbolize.NewDemuxer(&repo, symbolizer)
	presenter := symbolize.NewBasicPresenter(os.Stdout, painter.Enabled())

	tap := symbolize.NewTriggerTap()
	var dumpHandler *symbolize.DumpHandler
	if jsonOutput != "" {
		dumpHandler = &symbolize.DumpHandler{}
		tap.AddHandler(dumpHandler.HandleDump)
	}

	// Build the pipeline to start presenting.
	if err := symbolizer.Start(ctx); err != nil {
		log.Fatalf("%v\n", err)
	}
	inputLines := symbolize.StartParsing(ctx, os.Stdin)
	outputLines := demuxer.Start(ctx, inputLines)
	trash := symbolize.ComposePostProcessors(ctx, outputLines,
		tap,
		&symbolize.ContextPresenter{},
		&symbolize.OptimizeColor{},
		symbolize.NewBacktracePresenter(os.Stdout, presenter))
	symbolize.Consume(trash)

	// Once the pipeline has finished output all triggers
	if jsonOutput != "" {
		file, err := os.Create(jsonOutput)
		if err != nil {
			log.Fatalf("%v\n", err)
		}
		if err := dumpHandler.Write(file); err != nil {
			log.Fatalf("%v\n", err)
		}
	}
}
