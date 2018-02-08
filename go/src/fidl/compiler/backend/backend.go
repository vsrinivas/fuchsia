// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"fidl/compiler/backend/cpp"
	"fidl/compiler/backend/dart"
	"fidl/compiler/backend/types"
	"flag"
	"io/ioutil"
	"log"
	"os"
)

type GenerateFidl interface {
	GenerateFidl(fidl types.Root, config *types.Config) error
}

var generators = map[string]GenerateFidl{
	"cpp": cpp.FidlGenerator{},
	"dart": dart.FidlGenerator{},
}

func main() {
	// TODO(cramertj): replace with path to FIDL definition, then make
	// this tool generate the JSON by running the compiler.
	fidlJSONPath := flag.String("fidl-json", "",
		"relative path to the FIDL JSON representation")
	fidlStem := flag.String("fidl-stem", "",
		"base file name for files generated from this FIDL JSON.")
	rootGenDir := flag.String("root-gen-dir", "",
		"root directory for all generated files.")
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

	if *fidlJSONPath == "" || *fidlStem == "" || *rootGenDir == "" {
		flag.PrintDefaults()
		os.Exit(1)
	}

	bytes, err := ioutil.ReadFile(*fidlJSONPath)
	if err != nil {
		log.Fatalf("Error reading from %s: %v", *fidlJSONPath, err)
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

	config := &types.Config{
		[]string(generatorArgs),
		*fidlStem,
		*rootGenDir,
	}

	for _, generatorName := range generatorNames {
		if generator, ok := generators[generatorName]; ok {
			running++
			go func() {
				results <- generator.GenerateFidl(fidlData, config)
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
