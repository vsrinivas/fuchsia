// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"strings"

	"fidl/compiler/backend/cmdline"
	"fidl/compiler/backend/cpp"
	"fidl/compiler/backend/cpp_libfuzzer"
	"fidl/compiler/backend/golang"
	"fidl/compiler/backend/syzkaller"
	"fidl/compiler/backend/types"
)

type GenerateFidl interface {
	GenerateFidl(fidl types.Root, config *types.Config) error
}

var generators = map[string]GenerateFidl{
	"cpp":       cpp.NewFidlGenerator(),
	"go":        golang.NewFidlGenerator(),
	"libfuzzer": cpp_libfuzzer.NewFidlGenerator(),
	"syzkaller": syzkaller.NewFidlGenerator(),
}

func main() {
	baseFlags := cmdline.BaseFlags()

	validGenerators := make([]string, 0, len(generators))
	for name := range generators {
		validGenerators = append(validGenerators, name)
	}
	var generatorNames CommaSeparatedList
	flag.Var(&generatorNames, "generators",
		fmt.Sprintf(`comma-separated list of names of generators to run
valid generators: %s
for Dart, use the fidlgen_dart executable
for LLCPP, use the fidlgen_llcpp executable
for Rust, use the fidlgen_rust executable`,
			strings.Join(validGenerators, ", ")))
	flag.Parse()

	if !flag.Parsed() || !baseFlags.Valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	fidl, err := types.ReadJSONIr(*baseFlags.JsonPath)
	if err != nil {
		log.Fatal(err)
	}
	config := baseFlags.Config()

	running := 0
	results := make(chan error)
	didError := false
	generatorNames = []string(generatorNames)

	for _, generatorName := range generatorNames {
		if generator, ok := generators[generatorName]; ok {
			running++
			go func() {
				results <- generator.GenerateFidl(fidl, &config)
			}()
		} else {
			log.Printf("Error: generator %s not found", generatorName)
			didError = true
		}
	}

	for running > 0 {
		err := <-results
		if err != nil {
			log.Printf("Error running generator: %v", err)
			didError = true
		}
		running--
	}

	if didError {
		os.Exit(1)
	}
}
