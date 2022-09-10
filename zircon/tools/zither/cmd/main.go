// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"go/format"
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/flagmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither/asm"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither/c"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither/golang"
)

const (
	cBackend   string = "c"
	goBackend  string = "go"
	asmBackend string = "asm"
)

var supportedBackends = []string{cBackend, goBackend}

// Flag values, grouped into a struct to be kept out of the global namespace.
var flags struct {
	irFile          string
	backend         string
	outputManifest  string
	outputDir       string
	clangFormat     string
	clangFormatArgs flagmisc.StringsValue
}

func init() {
	flag.StringVar(&flags.irFile, "ir", "", "The FIDL IR JSON file from which bindings will be generated")
	flag.StringVar(&flags.backend, "backend", "", "The zither backend.\nSupported values: \""+strings.Join(supportedBackends, "\", \"")+"\"")
	flag.StringVar(&flags.outputManifest, "output-manifest", "", "A path to which a JSON list of the binding output files will be written, if specified. This list excludes the output manifest")
	flag.StringVar(&flags.outputDir, "output-dir", "", "The directory to which the bindings will be written. (The layout is backend-specific.)")
	flag.StringVar(&flags.clangFormat, "clang-format", "", "The path to `clang-format`, used to format bindings in the appropriate backends")
	flag.Var(&flags.clangFormatArgs, "clang-format-args", "Arguments to pass to `clang-format`, when used")
}

func main() {
	flag.Parse()

	l := logger.NewLogger(logger.InfoLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "zither: ")
	ctx := logger.WithLogger(context.Background(), l)

	if flags.irFile == "" {
		logger.Errorf(ctx, "`-ir` is a required argument")
		os.Exit(1)
	}

	cf := fidlgen.NewFormatter(flags.clangFormat, flags.clangFormatArgs...)

	var gen generator
	switch flags.backend {
	case cBackend:
		gen = c.NewGenerator(cf)
	case asmBackend:
		gen = asm.NewGenerator(cf)
	case goBackend:
		gen = golang.NewGenerator(goFormatter{})
	default:
		logger.Errorf(ctx, "unrecognized `-backend` value: %q", flags.backend)
		os.Exit(1)
	}

	ir, err := fidlgen.ReadJSONIr(flags.irFile)
	if err != nil {
		logger.Errorf(ctx, "%s", err)
		os.Exit(1)
	}

	if err := execute(ctx, gen, ir, flags.outputDir, flags.outputManifest); err != nil {
		logger.Errorf(ctx, "%s", err)
		os.Exit(1)
	}
}

// generator represents an abstract generator of bindings.
type generator interface {
	// DeclOrder gives the declaration order desired by the backend.
	DeclOrder() zither.DeclOrder

	// Generate generates bindings into the provided output directory.
	Generate(summaries []zither.FileSummary, outputDir string) ([]string, error)
}

func execute(ctx context.Context, gen generator, ir fidlgen.Root, outputDir, outputManifest string) error {
	summaries, err := zither.Summarize(ir, gen.DeclOrder())
	if err != nil {
		return err
	}

	outputs, err := gen.Generate(summaries, outputDir)
	if err != nil {
		return err
	}

	if outputManifest != "" {
		f, err := os.Create(outputManifest)
		if err != nil {
			return err
		}

		encoder := json.NewEncoder(f)
		encoder.SetIndent("", "\t")
		if err := encoder.Encode(outputs); err != nil {
			f.Close()
			return err
		}

		if err := f.Close(); err != nil {
			return err
		}
	}

	return nil
}

type goFormatter struct{}

func (f goFormatter) Format(source []byte) ([]byte, error) {
	return format.Source(source)
}
