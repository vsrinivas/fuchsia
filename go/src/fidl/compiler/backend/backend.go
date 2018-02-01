// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"fidl/compiler/backend/cpp"
	"fidl/compiler/backend/test"
	"fidl/compiler/backend/types"
)

type FileWriter interface {
}

type GenerateFidl interface {
	GenerateFidl(fidlData types.Root, args []string, outputDir string, srcRootPath string) error
}

var generators = map[string]GenerateFidl{
	"cpp": cpp.FidlGenerator{},
	"test": test.FidlGenerator{},
}

func main() {
	// TODO(cramertj): replace with path to FIDL definition, then make
	// this tool generate the JSON by running the compiler.
	fidlJsonPath := flag.String("fidl-json", "",
		"relative path to the FIDL JSON representation")
	outputDir := flag.String("output-dir", ".",
		"output directory for generated files.")
	srcRootPath := flag.String("src-root-path", ".",
		"relative path to the root of the source tree.")
	var generatorNames CommaSeparatedList
	flag.Var(&generatorNames, "generators",
		"Comma-separated list of names of generators to run")
	var generatorArgs RepeatedStringArg
	flag.Var(&generatorArgs, "gen-arg",
		"Argument to be passed to the generators. (e.g. --gen-arg fo=bar --gen-arg blah)")
	flag.Parse()

	if !flag.Parsed() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	// The fidlJsonPath is not an optional argument, so we error if it was omitted.
	if *fidlJsonPath == "" {
		flag.PrintDefaults()
		os.Exit(1)
	}
	fmt.Println(*fidlJsonPath)

	bytes, err := ioutil.ReadFile(*fidlJsonPath)
	if err != nil {
		log.Fatalf("Error reading from %s: %v", *fidlJsonPath, err)
	}

	var fidlData types.Root
	err = json.Unmarshal(bytes, &fidlData)
	if err != nil {
		log.Fatalf("Error parsing JSON as FIDL data: %v", err)
	}

	running := 0
	results := make(chan error)
	didError := false
	generatorNames = []string(generatorNames)
	generatorArgs = []string(generatorArgs)

	for _, generatorName := range generatorNames {
		if generator, ok := generators[generatorName]; ok {
			running++
			go func() {
				results <- generator.GenerateFidl(fidlData, generatorArgs, *outputDir, *srcRootPath)
			}()
		} else {
			log.Printf("Error: generator %s not found", generatorName)
			didError = true
		}
	}

	for running > 0 {
		err = <-results
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
