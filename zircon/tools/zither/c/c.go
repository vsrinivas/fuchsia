// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package c

import (
	"embed"
	"path/filepath"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither"
)

//go:embed templates/*
var templates embed.FS

// Generator provides C data layout bindings.
type Generator struct {
	*fidlgen.Generator
	outputFiles []string
}

func NewGenerator(formatter fidlgen.Formatter) *Generator {
	gen := fidlgen.NewGenerator("CTemplates", templates, formatter, template.FuncMap{})
	return &Generator{gen, nil}
}

func (gen *Generator) Generate(summary *zither.Summary, outputDir string) error {
	parts := summary.Name.Parts()
	dir := ""
	if len(parts) > 1 {
		dir = filepath.Join(parts[0 : len(parts)-1]...)
	}
	name := parts[len(parts)-1] + ".h"
	output := filepath.Join(outputDir, dir, "c", name)
	if err := gen.GenerateFile(output, "GenerateCFile", summary); err != nil {
		return err
	}
	gen.outputFiles = append(gen.outputFiles, output)
	return nil
}

func (gen Generator) Outputs() []string { return gen.outputFiles }
