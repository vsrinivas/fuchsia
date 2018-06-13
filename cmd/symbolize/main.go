// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
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
	jsonOutput    string
)

func init() {
	flag.StringVar(&llvmSymboPath, "llvm-symbolizer", "llvm-symbolizer", "path to llvm-symbolizer")
	flag.StringVar(&idsPath, "ids", "", "path to ids.txt")
	flag.StringVar(&colorValue, "color", "auto", "can be `always`, `auto`, or `never`.")
	flag.StringVar(&jsonOutput, "json-output", "", "outputs trigger information to the specified file")
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

type dumpEntry struct {
	Modules  []symbolize.Module  `json:"modules"`
	Segments []symbolize.Segment `json:"segments"`
	Type     string              `json:"type"`
	Name     string              `json:"name"`
}

type dumpHandler struct {
	dumps []dumpEntry
}

func (d *dumpHandler) HandleDump(dump *symbolize.DumpfileElement) {
	triggerCtx := dump.Context()
	d.dumps = append(d.dumps, dumpEntry{
		Modules:  triggerCtx.Mods,
		Segments: triggerCtx.Segs,
		Type:     dump.SinkType(),
		Name:     dump.Name(),
	})
}

func (d *dumpHandler) Write(buf io.Writer) error {
	enc := json.NewEncoder(buf)
	enc.SetIndent("", "  ")
	err := enc.Encode(d.dumps)
	if err != nil {
		return err
	}
	return nil
}

func main() {
	// Parse flags and setup helpers
	flag.Parse()
	painter, err := getColor()
	if err != nil {
		log.Fatal(err)
	}
	var jsonTriggerHandler *dumpHandler
	if jsonOutput != "" {
		jsonTriggerHandler = &dumpHandler{}
	}

	// Setup logger and context
	symbolizeLogger := logger.NewLogger(logger.WarningLevel, painter, os.Stderr, os.Stderr)
	ctx := logger.WithLogger(context.Background(), symbolizeLogger)

	// Construct the nodes of the pipeline
	symbolizer := symbolize.NewLLVMSymbolizer(llvmSymboPath)
	repo := symbolize.NewRepo()
	err = repo.AddSource(symbolize.NewIDsSource(idsPath))
	if err != nil {
		symbolizeLogger.Fatalf("%v", err)
	}
	demuxer := symbolize.NewDemuxer(repo, symbolizer)
	tap := symbolize.NewTriggerTap()
	if jsonTriggerHandler != nil {
		tap.AddHandler(jsonTriggerHandler.HandleDump)
	}
	presenter := symbolize.NewBasicPresenter(os.Stdout, painter.Enabled())

	// Build the pipeline to start presenting.
	err = symbolizer.Start(ctx)
	if err != nil {
		symbolizeLogger.Fatalf("%v", err)
	}
	inputLines := symbolize.StartParsing(ctx, os.Stdin)
	outputLines := demuxer.Start(ctx, inputLines)
	trash := symbolize.ComposePostProcessors(ctx, outputLines,
		tap,
		&symbolize.FilterContextElements{},
		&symbolize.OptimizeColor{},
		presenter)
	symbolize.Consume(trash)

	// Once the pipeline has finished output all triggers
	if jsonTriggerHandler != nil {
		file, err := os.Open(jsonOutput)
		if err != nil {
			log.Fatal(err)
		}
		jsonTriggerHandler.Write(file)
	}
}
