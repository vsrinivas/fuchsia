// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package kernel

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

// Kernel sources, given by its include path. Each file has a corresponding
// template named "Generate-${file basename}".
var includePaths = []string{
	filepath.Join("lib", "syscalls", "zx-syscall-numbers.h"),
	// TODO(fxbug.dev/110295):
	// filepath.Join("lib", "syscalls", "category.inc"),
	// filepath.Join("lib", "syscalls", "kernel-wrappers.inc"),
	// filepath.Join("lib", "syscalls", "kernel.inc"),
	// filepath.Join("lib", "syscalls", "syscalls.inc"),
}

type Generator struct {
	fidlgen.Generator
}

func NewInternalGenerator(formatter fidlgen.Formatter) *Generator {
	gen := fidlgen.NewGenerator("ZirconKernelTemplates", templates, formatter, template.FuncMap{
		"LowerCaseWithUnderscores": zither.LowerCaseWithUnderscores,
		"Increment":                Increment,
	})
	return &Generator{*gen}
}

func (gen Generator) DeclOrder() zither.DeclOrder { return zither.SourceDeclOrder }

func (gen *Generator) Generate(summaries []zither.FileSummary, outputDir string) ([]string, error) {
	var syscalls []zither.Syscall
	for _, summary := range summaries {
		for _, decl := range summary.Decls {
			if !decl.IsSyscallFamily() {
				continue
			}
			for _, syscall := range decl.AsSyscallFamily().Syscalls {
				syscalls = append(syscalls, syscall)
			}
		}
	}
	sort.Slice(syscalls, func(i, j int) bool {
		return strings.Compare(syscalls[i].Name, syscalls[j].Name) < 0
	})

	var outputs []string
	for _, file := range includePaths {
		output := filepath.Join(outputDir, file)
		templateName := "Generate-" + filepath.Base(file)
		if err := gen.GenerateFile(output, templateName, syscalls); err != nil {
			return nil, err
		}
		outputs = append(outputs, output)
	}
	return outputs, nil
}

//
// Template functions.
//

func Increment(n int) int { return n + 1 }
