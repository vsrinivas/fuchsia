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
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Generator struct {
	tmpls *template.Template
	flags *CmdlineFlags
}

func NewGenerator(flags *CmdlineFlags, extraFuncs template.FuncMap, templates []string) *Generator {
	gen := &Generator{
		template.New(flags.name), flags,
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
	for _, t := range templates {
		template.Must(gen.tmpls.Parse(t))
	}

	return gen
}

func (gen *Generator) ExperimentEnabled(experiment string) bool {
	return gen.flags.ExperimentEnabled(experiment)
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
	maybeFormatter := gen.flags.clangFormatPath
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
