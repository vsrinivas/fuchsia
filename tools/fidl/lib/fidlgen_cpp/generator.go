// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"bytes"
	"fmt"
	"io/fs"
	"log"
	"path"
	"runtime"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// TODO(fxbug.dev/49757) Use --style=file and copy the .clang-format file to the correct location.
// An alternate way to do this is to load the config directly from .clang_format and put the
// style as JSON in quotes.
var clangFormatArgs = []string{"--style=google"}

type Generator struct {
	tmpls     *template.Template
	flags     *CmdlineFlags
	formatter fidlgen.Formatter
}

func newGenerator(flags *CmdlineFlags, extraFuncs template.FuncMap) *Generator {
	gen := &Generator{
		template.New(flags.name), flags,
		fidlgen.NewFormatter(flags.clangFormatPath, clangFormatArgs...),
	}

	funcs := mergeFuncMaps(commonTemplateFuncs, extraFuncs)
	funcs = mergeFuncMaps(funcs, template.FuncMap{
		"Filename": func(name string, library fidlgen.LibraryIdentifier) string {
			return gen.generateFilename(name, library)
		},
		"ExperimentEnabled": func(experiment string) bool {
			return gen.ExperimentEnabled(experiment)
		},
	})
	gen.tmpls.Funcs(funcs)

	return gen
}

func NewGenerator(flags *CmdlineFlags, extraFuncs template.FuncMap, templates []string) *Generator {
	gen := newGenerator(flags, extraFuncs)
	for _, t := range templates {
		template.Must(gen.tmpls.Parse(t))
	}
	return gen
}

func NewGeneratorFS(flags *CmdlineFlags, extraFuncs template.FuncMap, templates fs.FS) *Generator {
	gen := newGenerator(flags, extraFuncs)
	template.Must(gen.tmpls.ParseFS(templates, "*"))
	return gen
}

func (gen *Generator) ExperimentEnabled(experiment string) bool {
	return gen.flags.ExperimentEnabled(experiment)
}

func (gen *Generator) generateFile(filename string, tmpl string, tree Root) error {
	var err error
	bufferedContent := new(bytes.Buffer)
	if err := gen.tmpls.ExecuteTemplate(bufferedContent, tmpl, tree); err != nil {
		return fmt.Errorf("Error generating content: %w", err)
	}

	var formatted []byte

	// TODO(fxbug.dev/78303): Investigate clang-format memory usage on large files.
	if bufferedContent.Len() > 1024*1024 && runtime.GOOS == "darwin" {
		formatted = bufferedContent.Bytes()
	} else {
		formatted, err = gen.formatter.Format(bufferedContent.Bytes())
		if err != nil {
			return fmt.Errorf("Error formatting: %w", err)
		}
	}
	return fidlgen.WriteFileIfChanged(filename, formatted)
}

type filenameData struct {
	Library fidlgen.LibraryIdentifier
}

func (fd *filenameData) joinLibraryParts(separator string) string {
	ss := make([]string, len(fd.Library))
	for i, s := range fd.Library {
		ss[i] = string(s)
	}
	return strings.Join(ss, separator)
}

func (fd *filenameData) LibraryDots() string {
	return fd.joinLibraryParts(".")
}

func (fd *filenameData) LibrarySlashes() string {
	return fd.joinLibraryParts("/")
}

func (gen *Generator) generateFilename(file string, library fidlgen.LibraryIdentifier) string {
	buf := new(bytes.Buffer)
	gen.tmpls.ExecuteTemplate(buf, "Filename:"+file, &filenameData{library})
	return buf.String()
}

func (gen *Generator) GenerateFiles(tree Root, files []string) {

	for _, f := range files {
		fn := path.Join(gen.flags.root, gen.generateFilename(f, tree.Library))
		err := gen.generateFile(fn, "File:"+f, tree)
		if err != nil {
			log.Fatalf("Error generating %s: %v", fn, err)
		}
	}

}
