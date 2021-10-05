// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"bytes"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"text/template"
)

type Generator struct {
	tmpls     *template.Template
	formatter Formatter
}

func NewGenerator(name string, templates fs.FS, formatter Formatter, funcs template.FuncMap) *Generator {
	gen := &Generator{
		template.New(name),
		formatter,
	}
	gen.tmpls.Funcs(funcs)
	template.Must(gen.tmpls.ParseFS(templates, "*"))
	return gen
}

func (gen *Generator) GenerateFile(filename string, tmpl string, data interface{}) error {
	if err := os.MkdirAll(filepath.Dir(filename), os.ModePerm); err != nil {
		return err
	}

	file, err := NewLazyWriter(filename)
	if err != nil {
		return fmt.Errorf("Error creating LazyWriter: %w", err)
	}

	bufferedContent := new(bytes.Buffer)
	if err := gen.tmpls.ExecuteTemplate(bufferedContent, tmpl, data); err != nil {
		return fmt.Errorf("Error generating content: %w", err)
	}

	generatedPipe, err := gen.formatter.FormatPipe(file)
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
