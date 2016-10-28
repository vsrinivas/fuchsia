// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// cli.go implements logic to create a generator config object from command
// line arguments.

package common

import (
	"flag"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"

	"fidl/bindings"
	"mojom/generated/mojom_files"
)

// GetCliConfig provides the primary interface for generators.
// By calling GetCliConfig, a generator implements the command line interface
// that is used by all generators.
func GetCliConfig(args []string) GeneratorConfig {
	flagSet := flag.NewFlagSet("Generator Common Flag Set", flag.ContinueOnError)
	return GetCliConfigWithFlagSet(args, flagSet)
}

func GetCliConfigWithFlagSet(args []string, flagSet *flag.FlagSet) GeneratorConfig {
	config := new(generatorCliConfig)

	var fileGraphFile string
	flagSet.StringVar(&fileGraphFile, "file-graph", "-",
		"Location of the parser output. \"-\" for stdin. (default \"-\")")
	flagSet.StringVar(&config.outputDir, "output-dir", ".",
		"output directory for generated files.")
	flagSet.StringVar(&config.srcRootPath, "src-root-path", ".",
		"relative path to the root of the source tree.")
	flagSet.BoolVar(&config.noGenImports, "no-gen-imports", false,
		"Generate code only for the files that are specified on the command line. "+
			"By default, code is generated for all specified files and their transitive imports.")
	flagSet.BoolVar(&config.genTypeInfo, "generate-type-info", false,
		"Do not generate type information inside the core.")

	err := flagSet.Parse(args[1:])
	if err == flag.ErrHelp {
		os.Exit(0)
	}

	// Compute the absolute path for the source root and the output directory.
	config.srcRootPath, err = filepath.Abs(config.srcRootPath)
	if err != nil {
		log.Fatalln(err.Error())
	}

	config.outputDir, err = filepath.Abs(config.outputDir)
	if err != nil {
		log.Fatalln(err.Error())
	}

	// Read the file graph either from standard in or the specified file.
	var fileGraphReader io.Reader
	if fileGraphFile == "-" {
		fileGraphReader = os.Stdin
	} else {
		var err error
		if fileGraphReader, err = os.Open(fileGraphFile); err != nil {
			log.Fatalln(err.Error())
		}
	}

	config.fileGraph = decodeFileGraph(fileGraphReader)
	return config
}

// decodeFileGraph decodes a serialized MojomFileGraph.
func decodeFileGraph(in io.Reader) (fileGraph *mojom_files.MojomFileGraph) {
	inputBytes, err := ioutil.ReadAll(in)
	if err != nil {
		log.Fatalf("Failed to read: %v\n", err)
	}

	decoder := bindings.NewDecoder(inputBytes, nil)
	fileGraph = new(mojom_files.MojomFileGraph)
	if err := fileGraph.Decode(decoder); err != nil {
		log.Fatalf("Failed to decode file graph: %v\n", err)
	}

	return
}

// generatorCliConfig implements GeneratorConfig.
type generatorCliConfig struct {
	fileGraph    *mojom_files.MojomFileGraph
	outputDir    string
	srcRootPath  string
	noGenImports bool
	genTypeInfo  bool
}

// See GeneratorConfig.
func (c *generatorCliConfig) FileGraph() *mojom_files.MojomFileGraph {
	return c.fileGraph
}

// See GeneratorConfig.
func (c *generatorCliConfig) OutputDir() string {
	return c.outputDir
}

// See GeneratorConfig.
func (c *generatorCliConfig) SrcRootPath() string {
	return c.srcRootPath
}

// See GeneratorConfig.
func (c *generatorCliConfig) GenImports() bool {
	return !c.noGenImports
}

// See GeneratorConfig.
func (c *generatorCliConfig) GenTypeInfo() bool {
	return c.genTypeInfo
}
