// Copyright 2019 The Fuchsia Authors. All rights reserved.
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
	"path"
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
	giidlwalker "gidl/walker"
)

// Generator is a function that generates conformance tests for a particular
// backend and returns a map of test name to file bytes. The test name is
// added as a suffix to the name of the file before the extension
// (e.g. my_file.go -> my_file_test_name.go).
type Generator func(gidlir.All, fidlir.Root) (map[string][]byte, error)

var conformanceGenerators = map[string]Generator{
	"go":    gidlgolang.GenerateConformanceTests,
	"llcpp": gidlllcpp.GenerateConformanceTests,
	"cpp":   gidlcpp.GenerateConformanceTests,
	"dart":  gidldart.GenerateConformanceTests,
	"rust":  gidlrust.GenerateConformanceTests,
}

var benchmarkGenerators = map[string]Generator{
	"go":     gidlgolang.GenerateBenchmarks,
	"llcpp":  gidlllcpp.GenerateBenchmarks,
	"cpp":    gidlcpp.GenerateBenchmarks,
	"rust":   gidlrust.GenerateBenchmarks,
	"dart":   gidldart.GenerateBenchmarks,
	"walker": giidlwalker.GenerateBenchmarks,
}

var allGenerators = map[string]map[string]Generator{
	"conformance": conformanceGenerators,
	"benchmark":   benchmarkGenerators,
}

var allGeneratorTypes = func() []string {
	var list []string
	for generatorType := range allGenerators {
		list = append(list, generatorType)
	}
	sort.Strings(list)
	return list
}()

var allLanguages = func() []string {
	var list []string
	seen := make(map[string]struct{})
	for _, generatorMap := range allGenerators {
		for language := range generatorMap {
			if _, ok := seen[language]; !ok {
				seen[language] = struct{}{}
				list = append(list, language)
			}
		}
	}
	sort.Strings(list)
	return list
}()

var allWireFormats = []gidlir.WireFormat{
	gidlir.V1WireFormat,
}

// GIDLFlags stores the command-line flags for the GIDL program.
type GIDLFlags struct {
	JSONPath *string
	Language *string
	Type     *string
	// TODO(fxb/52371) It should not be necessary to specify the number of generated files.
	NumOutputFiles *int
	Out            *string
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
	Type: flag.String("type", "", fmt.Sprintf("output type (%s)", strings.Join(allGeneratorTypes, "/"))),
	NumOutputFiles: flag.Int("num-output-files", 1,
		`Upper bound on number of output files.
	Used to split C++ outputs to multiple files.
	Must be at least as large as the actual number of outputs.`),
	Out: flag.String("out", "-", "optional path to write output to"),
}

func parseGidlIr(filename string) gidlir.All {
	f, err := os.Open(filename)
	if err != nil {
		panic(err)
	}
	config := gidlparser.Config{
		Languages:   allLanguages,
		WireFormats: allWireFormats,
	}
	result, err := gidlparser.NewParser(filename, f, config).Parse()
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

	// TODO(fxbug.dev/7802): While transitioning "zx" from [Internal] to a normal
	// library, tolerate but ignore a dependency on zx.
	if len(fidl.Libraries) == 1 && fidl.Libraries[0].Name == "zx" {
		fidl.Libraries = make([]fidlir.Library, 0)
	}

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

	gidlir.ValidateAllType(gidl, *flags.Type)
	generatorMap, ok := allGenerators[*flags.Type]
	if !ok {
		panic(fmt.Sprintf("unknown generator type: %s", *flags.Type))
	}
	generator, ok := generatorMap[language]
	if !ok {
		log.Fatalf("unknown language for %s: %s", *flags.Type, language)
	}

	files, err := generator(gidl, fidl)
	if err != nil {
		log.Fatal(err)
	}

	if len(files) > *flags.NumOutputFiles {
		log.Fatalf("more generated gidl outputs than the number of output files")
	}

	if *flags.Out == "-" {
		if len(files) != 1 {
			log.Fatalf("multiple files generated, can't output to stdout")
		}
		for _, file := range files {
			_, err = os.Stdout.Write(file)
			if err != nil {
				log.Fatal(err)
			}
		}
		return
	}

	os.Remove(filepath.Dir(*flags.Out)) // clear any existing directory
	if err := os.MkdirAll(filepath.Dir(*flags.Out), os.ModePerm); err != nil {
		log.Fatal(err)
	}
	// TODO(fxb/52371) The key in 'files' is the type name and should be used for naming files.
	fileNum := 1
	for _, file := range files {
		if err := ioutil.WriteFile(outputFilepath(*flags.Out, fileNum), file, 0666); err != nil {
			log.Fatal(err)
		}
		fileNum++
	}
	// Fill unused output files with empty files to have a consistent set of source dependencies
	// in GN.
	// TODO(fxb/52371) The empty file doesn't work for some languages, like go that require a
	// package to be declared.
	for ; fileNum <= *flags.NumOutputFiles; fileNum++ {
		if err := ioutil.WriteFile(outputFilepath(*flags.Out, fileNum), []byte{}, 0666); err != nil {
			log.Fatal(err)
		}
	}
}

// x/y/abc.test.go -> x/y/abc123.test.go
func outputFilepath(inputFilepath string, fileNum int) string {
	extra := ""
	if fileNum > 1 {
		extra = fmt.Sprintf("%d", fileNum)
	}
	dir, filename := path.Split(inputFilepath)
	newfilename := strings.Join(strings.SplitN(filename, ".", 2), fmt.Sprintf("%s.", extra))
	return path.Join(dir, newfilename)
}
