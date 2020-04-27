// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"
	"path"
)

type Flags struct {
	OutDir *string
}

var flags = Flags{
	OutDir: flag.String("out_dir", "",
		"[optional] Path to the output dir. Defaults to $FUCHSIA_DIR/src/tests/benchmarks/fidl/benchmark_suite"),
}

func main() {
	flag.Parse()

	outdir := *flags.OutDir
	if outdir == "" {
		fuchsiaDir, ok := os.LookupEnv("FUCHSIA_DIR")
		if !ok {
			log.Fatalf("Either --out_dir must be specified or FUCHSIA_DIR must be set")
		}
		outdir = path.Join(fuchsiaDir, "src/tests/benchmarks/fidl/benchmark_suite")
	}

	genFidl(outdir)
	genGidl(outdir)
}
