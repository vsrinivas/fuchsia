// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	"go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	"go.fuchsia.dev/fuchsia/tools/fidl/gidl/parser"
)

var corpusPaths = map[string]string{
	"conformance": "src/tests/fidl/conformance_suite",
	"benchmark":   "src/tests/benchmarks/fidl/benchmark_suite",
}

var corpusLanguages = map[string][]string{
	"conformance": {
		"c",
		"go",
		"llcpp",
		"hlcpp",
		"dart",
		"rust",
		"transformer",
		"fuzzer_corpus",
	},
	"benchmark": {
		"go",
		"llcpp",
		"hlcpp",
		"rust",
		"dart",
		"reference",
		"walker",
	},
}

var allWireFormats = []ir.WireFormat{
	ir.V1WireFormat,
	ir.V2WireFormat,
}

type auditFlags struct {
	corpusName *string
	language   *string
}

func (f auditFlags) valid() bool {
	if _, ok := corpusPaths[*f.corpusName]; !ok {
		fmt.Printf("unknown corpus name %s\n", *f.corpusName)
		return false
	}
	if _, ok := corpusLanguages[*f.corpusName]; !ok {
		panic("corpus listed in corpusPaths but not corpusLanguages")
	}
	if *f.language == "" {
		fmt.Printf("-language must be specified\n")
		return false
	}
	return true
}

var flags = auditFlags{
	corpusName: flag.String("corpus", "conformance", "corpus name (conformance or benchmark)"),
	language:   flag.String("language", "", "language to filter to"),
}

func main() {
	flag.Parse()

	if !flag.Parsed() || !flags.valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	fuchsiaDir, ok := os.LookupEnv("FUCHSIA_DIR")
	if !ok {
		fmt.Printf("FUCHSIA_DIR environment variable must be set")
		os.Exit(1)
	}

	globPattern := fuchsiaDir + "/" + corpusPaths[*flags.corpusName] + "/*.gidl"
	gidlFiles, err := filepath.Glob(globPattern)
	if err != nil {
		fmt.Printf("failed to match glob pattern %q when looking for GIDL files\n", globPattern)
		os.Exit(1)
	}
	all := parseAllGidlIr(gidlFiles)

	filtered := filter(all, *flags.language)

	fmt.Printf("Disabled tests for %s\n", *flags.language)
	fmt.Printf("***************************************\n")

	if len(filtered.EncodeSuccess) > 0 {
		fmt.Printf("\nEncode success\n")
		fmt.Printf("---------------------------------------\n")
		for _, t := range filtered.EncodeSuccess {
			fmt.Printf("%s\n", t.Name)
		}
	}

	if len(filtered.EncodeFailure) > 0 {
		fmt.Printf("\nEncode failure\n")
		fmt.Printf("---------------------------------------\n")
		for _, t := range filtered.EncodeFailure {
			fmt.Printf("%s\n", t.Name)
		}
	}

	if len(filtered.DecodeSuccess) > 0 {
		fmt.Printf("\nDecode success\n")
		fmt.Printf("---------------------------------------\n")
		for _, t := range filtered.DecodeSuccess {
			fmt.Printf("%s\n", t.Name)
		}
	}

	if len(filtered.DecodeFailure) > 0 {
		fmt.Printf("\nDecode failure\n")
		fmt.Printf("---------------------------------------\n")
		for _, t := range filtered.DecodeFailure {
			fmt.Printf("%s\n", t.Name)
		}
	}

	if len(filtered.Benchmark) > 0 {
		fmt.Printf("\nBenchmark\n")
		fmt.Printf("---------------------------------------\n")
		for _, t := range filtered.Benchmark {
			fmt.Printf("%s\n", t.Name)
		}
	}
}

func parseGidlIr(filename string) ir.All {
	f, err := os.Open(filename)
	if err != nil {
		panic(err)
	}
	config := parser.Config{
		Languages:   corpusLanguages[*flags.corpusName],
		WireFormats: allWireFormats,
	}
	result, err := parser.NewParser(filename, f, config).Parse()
	if err != nil {
		panic(err)
	}
	return result
}

func parseAllGidlIr(paths []string) ir.All {
	var parsedGidlFiles []ir.All
	for _, path := range paths {
		parsedGidlFiles = append(parsedGidlFiles, parseGidlIr(path))
	}
	return ir.Merge(parsedGidlFiles)
}

func filter(input ir.All, language string) ir.All {
	shouldKeep := func(allowlist *ir.LanguageList, denylist *ir.LanguageList) bool {
		if denylist != nil && denylist.Includes(language) {
			return true
		}
		if allowlist != nil {
			return !allowlist.Includes(language)
		}
		return ir.LanguageList(config.DefaultBindingsDenylist).Includes(language)
	}
	var output ir.All
	for _, def := range input.EncodeSuccess {
		if shouldKeep(def.BindingsAllowlist, def.BindingsDenylist) {
			output.EncodeSuccess = append(output.EncodeSuccess, def)
		}
	}
	for _, def := range input.DecodeSuccess {
		if shouldKeep(def.BindingsAllowlist, def.BindingsDenylist) {
			output.DecodeSuccess = append(output.DecodeSuccess, def)
		}
	}
	for _, def := range input.EncodeFailure {
		if shouldKeep(def.BindingsAllowlist, def.BindingsDenylist) {
			output.EncodeFailure = append(output.EncodeFailure, def)
		}
	}
	for _, def := range input.DecodeFailure {
		if shouldKeep(def.BindingsAllowlist, def.BindingsDenylist) {
			output.DecodeFailure = append(output.DecodeFailure, def)
		}
	}
	for _, def := range input.Benchmark {
		if shouldKeep(def.BindingsAllowlist, def.BindingsDenylist) {
			output.Benchmark = append(output.Benchmark, def)
		}
	}
	return output
}
