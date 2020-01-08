// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"

	fidlir "fidl/compiler/backend/types"
	gidlcpp "gidl/cpp"
	gidldart "gidl/dart"
	gidlgolang "gidl/golang"
	gidlir "gidl/ir"
	gidlllcpp "gidl/llcpp"
	gidlparser "gidl/parser"
	gidlrust "gidl/rust"
	gidltransformer "gidl/transformer"
)

// Generator is a function that generates conformance tests for a particular
// backend and writes them to the io.Writer.
type Generator func(io.Writer, gidlir.All, fidlir.Root) error

var generators = map[string]Generator{
	"go":          gidlgolang.Generate,
	"cpp":         gidlcpp.Generate,
	"llcpp":       gidlllcpp.Generate,
	"dart":        gidldart.Generate,
	"rust":        gidlrust.Generate,
	"transformer": gidltransformer.Generate,
}

var allLanguages = func() []string {
	var list []string
	for language := range generators {
		list = append(list, language)
	}
	sort.Strings(list)
	return list
}()

// GIDLFlags stores the command-line flags for the GIDL program.
type GIDLFlags struct {
	JSONPath *string
	Language *string
	Out      *string
}

// Valid indicates whether the parsed Flags are valid to be used.
func (gidlFlags GIDLFlags) Valid() bool {
	return len(*gidlFlags.JSONPath) != 0 && flag.NArg() != 0
}

var flags = GIDLFlags{
	JSONPath: flag.String("json", "",
		"relative path to the FIDL intermediate representation."),
	Language: flag.String("language", "",
		fmt.Sprintf("target language (%s)", strings.Join(allLanguages, "/"))),
	Out: flag.String("out", "-", "optional path to write output to"),
}

func parseGidlIr(filename string) gidlir.All {
	f, err := os.Open(filename)
	if err != nil {
		panic(err)
	}
	result, err := gidlparser.NewParser(filename, f, allLanguages).Parse()
	if err != nil {
		panic(err)
	}
	return result
}

func parseFidlJSONIr(filename string) fidlir.Root {
	bytes, err := ioutil.ReadFile(filename)
	if err != nil {
		panic(err)
	}
	var result fidlir.Root
	if err := json.Unmarshal(bytes, &result); err != nil {
		panic(err)
	}
	return result
}

func main() {
	flag.Parse()

	if !flag.Parsed() || !flags.Valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	fidl := parseFidlJSONIr(*flags.JSONPath)

	var parsedGidlFiles []gidlir.All
	for _, path := range flag.Args() {
		parsedGidlFiles = append(parsedGidlFiles, parseGidlIr(path))
	}
	gidl := gidlir.FilterByBinding(gidlir.Merge(parsedGidlFiles), *flags.Language)

	// For simplicity, we do not allow FIDL that GIDL depends on to have
	// dependent libraries. This makes it much simpler to have everything
	// in the IR, and avoid cross-references.
	if len(fidl.Libraries) != 0 {
		var libs []string
		for _, l := range fidl.Libraries {
			libs = append(libs, string(l.Name))
		}
		panic(fmt.Sprintf(
			"GIDL does not work with FIDL libraries with dependents, found: %s",
			strings.Join(libs, ",")))
	}

	language := *flags.Language
	if language == "" {
		panic("must specify --language")
	}
	buf := new(bytes.Buffer)
	generator, ok := generators[language]
	if !ok {
		panic(fmt.Sprintf("unknown language: %s", language))
	}

	err := generator(buf, gidl, fidl)
	if err != nil {
		panic(err)
	}
	var writer = os.Stdout
	if *flags.Out != "-" {
		err := os.MkdirAll(filepath.Dir(*flags.Out), os.ModePerm)
		if err != nil {
			panic(err)
		}
		writer, err = os.Create(*flags.Out)
		if err != nil {
			panic(err)
		}
		defer writer.Close()
	}

	_, err = writer.Write(buf.Bytes())
	if err != nil {
		panic(err)
	}
}
