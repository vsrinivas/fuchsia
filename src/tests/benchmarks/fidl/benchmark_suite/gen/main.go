// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package main

import (
	"flag"
	"log"
	"os"
	"path"

	// Register fidl and gidl files.
	_ "go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/fidl"
	_ "go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/gidl"
)

type allFlags struct {
	OutDir *string
}

var flags = allFlags{
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
