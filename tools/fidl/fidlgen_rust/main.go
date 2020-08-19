// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path"

	"fidl/compiler/backend/types"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_rust/codegen"
)

type flagsDef struct {
	jsonPath           *string
	outputFilenamePath *string
	rustfmtPath        *string
	rustfmtConfigPath  *string
}

var flags = flagsDef{
	jsonPath: flag.String("json", "",
		"relative path to the FIDL intermediate representation."),
	outputFilenamePath: flag.String("output-filename", "",
		"the output path for the generated Rust implementation."),
	rustfmtPath: flag.String("rustfmt", "",
		"path to the rustfmt tool."),
	rustfmtConfigPath: flag.String("rustfmt-config", "",
		"path to rustfmt.toml."),
}

// valid returns true if the parsed flags are valid.
func (f flagsDef) valid() bool {
	return *f.jsonPath != "" && *f.outputFilenamePath != ""
}

func printUsage() {
	program := path.Base(os.Args[0])
	message := `Usage: ` + program + ` [flags]

Rust FIDL backend, used to generate Rust bindings from JSON IR input (the
intermediate representation of a FIDL library).

Flags:
`
	fmt.Fprint(flag.CommandLine.Output(), message)
	flag.PrintDefaults()
}

func main() {
	flag.Usage = printUsage
	flag.Parse()
	if !flag.Parsed() || !flags.valid() {
		printUsage()
		os.Exit(1)
	}

	root, err := types.ReadJSONIr(*flags.jsonPath)
	if err != nil {
		log.Fatal(err)
	}

	generator := codegen.NewGenerator()
	err = generator.GenerateFidl(
		root, *flags.outputFilenamePath, *flags.rustfmtPath, *flags.rustfmtConfigPath)
	if err != nil {
		log.Fatalf("Error running generator: %v", err)
	}
}
