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

	bufferedContent := new(bytes.Buffer)
	if err := gen.tmpls.ExecuteTemplate(bufferedContent, tmpl, data); err != nil {
		return fmt.Errorf("Error generating content: %w", err)
	}

	formatted, err := gen.formatter.Format(bufferedContent.Bytes())
	if err != nil {
		return fmt.Errorf("Error formatting source: %w", err)
	}

	return WriteFileIfChanged(filename, formatted)
}
