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
	"fidl/compiler/backend/types"
	"fidl/compiler/llcpp_backend/llcpp"
)

// commaSeparatedList holds the result of parsing a command-line flag that
// accepts a comma-separated list of strings. This type satisfies the flag.Value
// interface.
type commaSeparatedList []string

func (l *commaSeparatedList) String() string {
	return fmt.Sprintf("%v", *l)
}

func (l *commaSeparatedList) Set(args string) error {
	for _, el := range strings.Split(args, ",") {
		*l = append(*l, el)
	}
	return nil
}

type GenerateFidl interface {
	GenerateFidl(fidl types.Root, config *types.Config) error
}

var generators = map[string]GenerateFidl{
	"llcpp":            llcpp.NewFidlGenerator(),
}

func main() {
	baseFlags := cmdline.BaseFlags()
	var generatorNames commaSeparatedList
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
