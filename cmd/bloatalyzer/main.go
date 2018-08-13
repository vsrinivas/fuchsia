// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"io"
	"os"

	"fuchsia.googlesource.com/tools/bloaty"
	"fuchsia.googlesource.com/tools/logger"
)

var (
	bloatyPath string
	idsPath    string
	output     string
)

func init() {
	flag.StringVar(&bloatyPath, "b", "", "path to bloaty executable")
	flag.StringVar(&idsPath, "i", "", "path to ids.txt")
	flag.StringVar(&output, "o", "", "output path")
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

	data, err := bloaty.RunBloaty(bloatyPath, idsPath)
	if err != nil {
		logger.Fatalf(ctx, "%v", err)
	}

	logger.Debugf(ctx, "Marshalling json...")
	json_out, err := json.Marshal(data)
	if err != nil {
		logger.Fatalf(ctx, "%v", err)
	}

	var out io.Writer
	if output != "" {
		var err error
		out, err = os.Create(output)
		if err != nil {
			logger.Fatalf(ctx, "uanble to open file: %v", err)
		}
	} else {
		out = os.Stdout
	}

	logger.Debugf(ctx, "Writing data...")
	if _, err = out.Write(json_out); err != nil {
		logger.Fatalf(ctx, "%v", err)
		return
	}
}
