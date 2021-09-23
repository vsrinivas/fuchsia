// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"bytes"
	"fmt"
	"log"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type GeneratedFile struct {
	Filename string
	Template string
}

type Generator struct {
	tmpls           *template.Template
	clangFormatPath string
}

func NewGenerator(name string, clangFormatPath string, extraFuncs template.FuncMap, templates []string) *Generator {
	tmpls := template.New(name).Funcs(mergeFuncMaps(commonTemplateFuncs, extraFuncs))
	for _, t := range templates {
		template.Must(tmpls.Parse(t))
	}

	return &Generator{
		tmpls, clangFormatPath,
	}
}

func (gen *Generator) generateFile(filename string, tmpl string, tree Root) error {
	if err := os.MkdirAll(filepath.Dir(filename), os.ModePerm); err != nil {
		return err
	}

	file, err := fidlgen.NewLazyWriter(filename)
	if err != nil {
		return fmt.Errorf("Error creating LazyWriter: %w", err)
	}

	bufferedContent := new(bytes.Buffer)
	if err := gen.tmpls.ExecuteTemplate(bufferedContent, tmpl, tree); err != nil {
		return fmt.Errorf("Error generating content: %w", err)
	}
	// TODO(fxbug.dev/78303): Investigate clang-format memory usage on large files.
	maybeFormatter := gen.clangFormatPath
	if bufferedContent.Len() > 1024*1024 && runtime.GOOS == "darwin" {
		maybeFormatter = ""
	}
	generatedPipe, err := NewClangFormatter(maybeFormatter).FormatPipe(file)
	if err != nil {
		return fmt.Errorf("Error in FormatPipe: %w", err)
	}
	_, err = bufferedContent.WriteTo(generatedPipe)
	if err != nil {
		return fmt.Errorf("Error writing to formatter: %w", err)
	}

	if err := generatedPipe.Close(); err != nil {
		return fmt.Errorf("Error closing generatedPipe: %w", err)
	}

	return nil
}

func (gen *Generator) GenerateFiles(base string, tree Root, files []GeneratedFile) {
	if !path.IsAbs(base) {
		cwd, err := os.Getwd()
		if err != nil {
			log.Fatalf("Getwd failed: %v", err)
		}
		base = path.Join(cwd, base)
	}
	for _, f := range files {
		var fn string
		if path.IsAbs(f.Filename) {
			fn = f.Filename
		} else {
			fn = path.Join(base, f.Filename)
		}
		err := gen.generateFile(fn, f.Template, tree)
		if err != nil {
			log.Fatalf("Error generating %s: %v", fn, err)
		}
	}

}
