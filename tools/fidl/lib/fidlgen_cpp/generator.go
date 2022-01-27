// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
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
	gen       *fidlgen.Generator
	flags     *CmdlineFlags
	formatter fidlgen.Formatter
}

func NewGenerator(flags *CmdlineFlags, templates fs.FS, extraFuncs template.FuncMap) *Generator {
	gen := &Generator{flags: flags}

	funcs := mergeFuncMaps(commonTemplateFuncs, extraFuncs)
	funcs = mergeFuncMaps(funcs, template.FuncMap{
		"Filename": func(name string, library fidlgen.LibraryIdentifier) string {
			return gen.generateFilename(name, library)
		},
		"ExperimentEnabled": func(experiment string) bool {
			return gen.ExperimentEnabled(experiment)
		},
	})

	var formatter fidlgen.Formatter

	if runtime.GOOS == "darwin" {
		// TODO(fxbug.dev/78303): Investigate clang-format memory usage on large files.
		formatter = fidlgen.NewFormatterWithSizeLimit(512*1024,
			flags.clangFormatPath, clangFormatArgs...)
	} else {
		formatter = fidlgen.NewFormatter(flags.clangFormatPath, clangFormatArgs...)
	}

	gen.gen = fidlgen.NewGenerator(flags.name, templates, formatter, funcs)
	return gen
}

func (gen *Generator) ExperimentEnabled(experiment string) bool {
	return gen.flags.ExperimentEnabled(experiment)
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
	fn, err := gen.gen.ExecuteTemplate("Filename:"+file, &filenameData{library})
	if err != nil {
		log.Fatalf("Error generating filename for %s: %v", file, err)
	}
	return string(fn)
}

func (gen *Generator) GenerateFiles(tree *Root, files []string) {
	for _, f := range files {
		fn := path.Join(gen.flags.root, gen.generateFilename(f, tree.Library))
		err := gen.gen.GenerateFile(fn, "File:"+f, tree)
		if err != nil {
			log.Fatalf("Error generating %s: %v", fn, err)
		}
	}
}
