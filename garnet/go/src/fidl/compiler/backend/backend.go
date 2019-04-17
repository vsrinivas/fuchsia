// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"

	"fidl/compiler/backend/cmdline"
	"fidl/compiler/backend/cpp"
	"fidl/compiler/backend/cpp_overnet_embedded"
	"fidl/compiler/backend/cpp_overnet_internal"
	"fidl/compiler/backend/golang"
	"fidl/compiler/backend/rust"
	"fidl/compiler/backend/syzkaller"
	"fidl/compiler/backend/types"
)

type GenerateFidl interface {
	GenerateFidl(fidl types.Root, config *types.Config) error
}

var generators = map[string]GenerateFidl{
	"cpp":              cpp.NewFidlGenerator(),
	"overnet_internal": cpp_overnet_internal.NewFidlGenerator(),
	"overnet_embedded": cpp_overnet_embedded.NewFidlGenerator(),
	"go":               golang.NewFidlGenerator(),
	"rust":             rust.NewFidlGenerator(),
	"syzkaller":        syzkaller.FidlGenerator{},
}

func main() {
	baseFlags := cmdline.BaseFlags()
	var generatorNames CommaSeparatedList
	flag.Var(&generatorNames, "generators",
		"Comma-separated list of names of generators to run")
	flag.Parse()

	if !flag.Parsed() || !baseFlags.Valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	fidl := baseFlags.FidlTypes()
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
