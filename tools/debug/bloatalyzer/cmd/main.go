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
	"os"
	"runtime"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/debug/bloaty"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type Format int

const (
	FormatJSON Format = iota
	FormatHTML
)

func (f *Format) Set(value string) error {
	switch strings.ToLower(value) {
	case "json":
		*f = FormatJSON
	case "html":
		*f = FormatHTML
	default:
		return fmt.Errorf("Unknown format type.")
	}
	return nil
}

func (f *Format) String() string {
	switch *f {
	case FormatJSON:
		return "json"
	case FormatHTML:
		return "html"
	default:
		return "unknown"
	}
}

var (
	bloatyPath string
	idsPath    string
	output     string
	format     Format
	title      string
	topFiles   uint64
	topSyms    uint64
	jobs       int
)

func init() {
	flag.StringVar(&bloatyPath, "bloaty", "", "path to bloaty executable")
	flag.StringVar(&idsPath, "input", "", "path to ids.txt")
	flag.StringVar(&output, "output", "", "output path")
	flag.Var(&format, "format", "output format (options: json, html)")
	flag.StringVar(&title, "title", "Bloaty Analysis", "title for html page")
	flag.Uint64Var(&topFiles, "top-files", 0, "max number of files to keep")
	flag.Uint64Var(&topSyms, "top-syms", 0, "max number of symbols to keep per file")
	flag.IntVar(&jobs, "jobs", runtime.NumCPU(), "max number of concurrent bloaty runs")
}

func main() {
	flag.Parse()
	if flag.NArg() != 0 {
		flag.PrintDefaults()
	}

	ctx := context.Background()

	if bloatyPath == "" {
		logger.Fatalf(ctx, "%s", "must provide path to bloaty executable.")
	}

	if idsPath == "" {
		logger.Fatalf(ctx, "%s", "must provide path to ids.txt file.")
	}

	if jobs <= 0 {
		logger.Fatalf(ctx, "%s", "jobs (j) must be greater than 0.")
	}

	data, err := bloaty.RunBloaty(bloatyPath, idsPath, topFiles, topSyms, jobs)
	if err != nil {
		logger.Fatalf(ctx, "%v", err)
	}

	var out io.Writer
	if output != "" {
		var err error
		f, err := os.Create(output)
		if err != nil {
			logger.Fatalf(ctx, "uanble to open file: %v", err)
		}
		defer f.Close()
		out = f
	} else {
		out = os.Stdout
	}

	switch format {
	case FormatHTML:
		logger.Debugf(ctx, "Writing HTML...")
		err = bloaty.Chart(data, title, out)
		if err != nil {
			logger.Fatalf(ctx, "%v", err)
		}
	case FormatJSON:
		logger.Debugf(ctx, "Marshalling JSON...")
		enc := json.NewEncoder(out)
		err := enc.Encode(data)
		if err != nil {
			logger.Fatalf(ctx, "%v", err)
		}
	default:
		logger.Fatalf(ctx, "Unknown format.")
	}
}
