// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zircon_ifs

import (
	"embed"
	"path/filepath"
	"sort"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither"
)

//go:embed templates/*
var templates embed.FS

type Generator struct {
	fidlgen.Generator
}

func NewGenerator(formatter fidlgen.Formatter) *Generator {
	gen := fidlgen.NewGenerator("ZirconIfsTemplates", templates, formatter, template.FuncMap{})
	return &Generator{*gen}
}

func (gen Generator) DeclOrder() zither.DeclOrder { return zither.SourceDeclOrder }

func (gen *Generator) Generate(summaries []zither.FileSummary, outputDir string) ([]string, error) {
	var symbols []symbol
	for _, summary := range summaries {
		for _, decl := range summary.Decls {
			if !decl.IsSyscallFamily() {
				continue
			}
			family := decl.AsSyscallFamily()
			for _, syscall := range family.Syscalls {
				if syscall.Internal {
					continue
				}
				name := "zx_" + zither.LowerCaseWithUnderscores(syscall)
				symbols = append(symbols,
					symbol{Name: "_" + name, Weak: false},
					symbol{Name: name, Weak: true},
				)
			}
		}
	}
	sort.Slice(symbols, func(i, j int) bool {
		return strings.Compare(symbols[i].Name, symbols[j].Name) < 0
	})

	output := filepath.Join(outputDir, "zircon.ifs")
	if err := gen.GenerateFile(output, "GenerateZirconIfsFile", symbols); err != nil {
		return nil, err
	}
	return []string{output}, nil
}

type symbol struct {
	Name string
	Weak bool
}
