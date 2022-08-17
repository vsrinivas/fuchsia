// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"sort"
	"strings"

	gidlc "go.fuchsia.dev/fuchsia/tools/fidl/gidl/c"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/cpp"
	gidldart "go.fuchsia.dev/fuchsia/tools/fidl/gidl/dart"
	gidldrivercpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/driver_cpp"
	gidldriverllcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/driver_llcpp"
	gidldynfidl "go.fuchsia.dev/fuchsia/tools/fidl/gidl/dynfidl"
	gidlcorpus "go.fuchsia.dev/fuchsia/tools/fidl/gidl/fuzzer_corpus"
	gidlgolang "go.fuchsia.dev/fuchsia/tools/fidl/gidl/golang"
	gidlhlcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/hlcpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlllcpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/llcpp"
	gidlparser "go.fuchsia.dev/fuchsia/tools/fidl/gidl/parser"
	gidlreference "go.fuchsia.dev/fuchsia/tools/fidl/gidl/reference"
	gidlrust "go.fuchsia.dev/fuchsia/tools/fidl/gidl/rust"
	gidlwalker "go.fuchsia.dev/fuchsia/tools/fidl/gidl/walker"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Generator is a function that generates conformance tests for a particular
// backend and returns a map of test name to file bytes. The test name is
// added as a suffix to the name of the file before the extension
// (e.g. my_file.go -> my_file_test_name.go).
// The first file is the "main output file".
type Generator func(gidlir.All, fidlgen.Root, gidlconfig.GeneratorConfig) ([]byte, error)

var conformanceGenerators = map[string]Generator{
	"c":             gidlc.GenerateConformanceTests,
	"dynfidl":       gidldynfidl.GenerateConformanceTests,
	"go":            gidlgolang.GenerateConformanceTests,
	"cpp":           gidlcpp.GenerateConformanceTests,
	"llcpp":         gidlllcpp.GenerateConformanceTests,
	"hlcpp":         gidlhlcpp.GenerateConformanceTests,
	"dart":          gidldart.GenerateConformanceTests,
	"rust":          gidlrust.GenerateConformanceTests,
	"fuzzer_corpus": gidlcorpus.GenerateConformanceTests,
}

var benchmarkGenerators = map[string]Generator{
	"go":           gidlgolang.GenerateBenchmarks,
	"cpp":          gidlcpp.GenerateBenchmarks,
	"llcpp":        gidlllcpp.GenerateBenchmarks,
	"hlcpp":        gidlhlcpp.GenerateBenchmarks,
	"rust":         gidlrust.GenerateBenchmarks,
	"dart":         gidldart.GenerateBenchmarks,
	"reference":    gidlreference.GenerateBenchmarks,
	"walker":       gidlwalker.GenerateBenchmarks,
	"driver_cpp":   gidldrivercpp.GenerateBenchmarks,
	"driver_llcpp": gidldriverllcpp.GenerateBenchmarks,
}

var measureTapeGenerators = map[string]Generator{
	"rust": gidlrust.GenerateMeasureTapeTests,
}

var allGenerators = map[string]map[string]Generator{
	"conformance":  conformanceGenerators,
	"benchmark":    benchmarkGenerators,
	"measure_tape": measureTapeGenerators,
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
	gidlir.V2WireFormat,
}

type listOfStrings []string

func (l *listOfStrings) String() string {
	return strings.Join(*l, " ")
}

func (l *listOfStrings) Set(s string) error {
	*l = append(*l, s)
	return nil
}

// GIDLFlags stores the command-line flags for the GIDL program.
type GIDLFlags struct {
	JSONPath                   *string
	Language                   *string
	Type                       *string
	Out                        *string
	RustBenchmarksFidlLibrary  *string
	CppBenchmarksFidlLibrary   *string
	FuzzerCorpusHostDir        *string
	FuzzerCorpusPackageDataDir *string
	FilterTypes                listOfStrings
}

// valid indicates whether the parsed Flags are valid to be used.
func (gidlFlags GIDLFlags) valid() bool {
	return len(*gidlFlags.JSONPath) != 0 && flag.NArg() != 0
}

var flags = GIDLFlags{
	JSONPath: flag.String("json", "",
		"relative path to the FIDL intermediate representation."),
	Language: flag.String("language", "",
		fmt.Sprintf("target language (%s)", strings.Join(allLanguages, "/"))),
	Type: flag.String("type", "", fmt.Sprintf("output type (%s)", strings.Join(allGeneratorTypes, "/"))),
	Out:  flag.String("out", "", "path to write output to"),
	RustBenchmarksFidlLibrary: flag.String("rust-benchmarks-fidl-library", "",
		"name for the fidl library used in the rust benchmarks"),
	CppBenchmarksFidlLibrary: flag.String("cpp-benchmarks-fidl-library", "",
		"name for the fidl library used in the cpp benchmarks"),
	FuzzerCorpusHostDir: flag.String("fuzzer-corpus-host-dir", "",
		"output directory for fuzzer_corpus"),
	FuzzerCorpusPackageDataDir: flag.String("fuzzer-corpus-package-data-dir", "",
		"directory to which fuzzer_corpus output files are mapped in their fuchsia package's data directory"),
	FilterTypes: nil,
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

func parseFidlJSONIr(filename string) fidlgen.Root {
	bytes, err := os.ReadFile(filename)
	if err != nil {
		panic(err)
	}
	var result fidlgen.Root
	if err := json.Unmarshal(bytes, &result); err != nil {
		panic(err)
	}
	return result
}

func main() {
	flag.Var(&flags.FilterTypes, "filter-type", "List of types to filter to (for measure tape backends)")
	flag.Parse()

	if !flags.valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	var config gidlconfig.GeneratorConfig
	if *flags.RustBenchmarksFidlLibrary != "" {
		config.RustBenchmarksFidlLibrary = *flags.RustBenchmarksFidlLibrary
	}
	if *flags.CppBenchmarksFidlLibrary != "" {
		config.CppBenchmarksFidlLibrary = *flags.CppBenchmarksFidlLibrary
	}
	if *flags.FuzzerCorpusHostDir != "" {
		config.FuzzerCorpusHostDir = *flags.FuzzerCorpusHostDir
	}
	if *flags.FuzzerCorpusPackageDataDir != "" {
		config.FuzzerCorpusPackageDataDir = *flags.FuzzerCorpusPackageDataDir
	}
	config.FilterTypes = []string(flags.FilterTypes)

	ir := parseFidlJSONIr(*flags.JSONPath)

	var parsedGidlFiles []gidlir.All
	for _, path := range flag.Args() {
		parsedGidlFiles = append(parsedGidlFiles, parseGidlIr(path))
	}
	gidl := gidlir.FilterByBinding(gidlir.Merge(parsedGidlFiles), *flags.Language)

	// For simplicity, we do not allow FIDL that GIDL depends on to have
	// dependent libraries, with the exception of zx. This makes it much simpler
	// to have everything in the IR, and avoid cross-references.

	if len(ir.Libraries) == 1 && ir.Libraries[0].Name == "zx" {
		ir.Libraries = make([]fidlgen.Library, 0)
	}

	if len(ir.Libraries) != 0 {
		var libs []string
		for _, l := range ir.Libraries {
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

	mainFile, err := generator(gidl, ir, config)
	if err != nil {
		log.Fatal(err)
	}

	if *flags.Out == "" {
		log.Fatalf("no -out path specified for main file")
	}

	if *flags.Language == "fuzzer_corpus" {
		// The fuzzer corpus manifest must always be written so that the build
		// system tries to rebuild the package. The individual files within the
		// corpus aren't tracked by the build system.
		err = os.WriteFile(*flags.Out, mainFile, 0666)
	} else {
		err = fidlgen.WriteFileIfChanged(*flags.Out, mainFile)
	}
	if err != nil {
		log.Fatal(err)
	}
}
