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

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_go/codegen"
)

type flagsDef struct {
	jsonPath          *string
	outputImplPath    *string
	outputPkgNamePath *string
}

var flags = flagsDef{
	jsonPath: flag.String("json", "",
		"relative path to the FIDL intermediate representation."),
	outputImplPath: flag.String("output-impl", "",
		"output path for the generated Go implementation."),
	outputPkgNamePath: flag.String("output-pkg-name", "",
		"output path for the generated Go implementation."),
}

// valid returns true if the parsed flags are valid.
func (f flagsDef) valid() bool {
	return *f.jsonPath != ""
}

func printUsage() {
	program := path.Base(os.Args[0])
	message := `Usage: ` + program + ` [flags]

Go FIDL backend, used to generate Go bindings from JSON IR input (the
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
	tree := codegen.Compile(root)

	if outputImplPath := *flags.outputImplPath; outputImplPath != "" {
		if err := generator.GenerateImplFile(tree, outputImplPath); err != nil {
			log.Fatalf("Error generating impl file: %v", err)
		}
	}

	if outputPkgNamePath := *flags.outputPkgNamePath; outputPkgNamePath != "" {
		if err := generator.GeneratePkgNameFile(tree, outputPkgNamePath); err != nil {
			log.Fatalf("Error generating pkg-name file: %v", err)
		}
	}
}
