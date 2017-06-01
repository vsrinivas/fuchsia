// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"
	"path"
	"path/filepath"
	"text/template"

	"fidl/compiler/generators/common"
	"fidl/compiler/generators/go/templates"
	"fidl/compiler/generators/go/translator"
)

func main() {
	log.SetFlags(0)
	flagSet := flag.NewFlagSet("Generator Go Flag Set", flag.ContinueOnError)
	var noGoSrc bool
	flagSet.BoolVar(&noGoSrc, "no-go-src", false, "Do not prepend the output path with go/src.")

	config := common.GetCliConfigWithFlagSet(os.Args, flagSet)
	t := translator.NewTranslator(config.FileGraph())
	goConfig := goConfig{config, t, noGoSrc}
	t.Config = goConfig
	common.GenerateOutput(genFile, goConfig)
}

type goConfig struct {
	common.GeneratorConfig
	translator translator.Translator
	noGoSrc    bool
}

func (c goConfig) OutputDir() string {
	if c.noGoSrc {
		return c.GeneratorConfig.OutputDir()
	} else {
		return filepath.Join(c.GeneratorConfig.OutputDir(), "go", "src")
	}
}

// stripExt removes the extension of a file.
func stripExt(fileName string) string {
	return fileName[:len(fileName)-len(filepath.Ext(fileName))]
}

func genFile(fileName string, config common.GeneratorConfig) {
	relFileName, err := filepath.Rel(config.SrcRootPath(), fileName)
	if err != nil {
		log.Fatal(err)
	}
	base := stripExt(filepath.Base(fileName))
	out := filepath.Join(config.OutputDir(), filepath.Dir(relFileName), base, base+".core.go")

	if err := os.MkdirAll(path.Dir(out), 0777); err != nil {
		log.Fatal(err)
	}
	f, err := os.Create(out)
	if err != nil {
		log.Fatal(err)
	}

	goConfig := config.(goConfig)
	fileTmpl := goConfig.translator.TranslateFidlFile(fileName)
	funcMap := template.FuncMap{
		"TypesPkg":    fileTmpl.TypesPkg,
		"DescPkg":     fileTmpl.DescPkg,
		"GenTypeInfo": config.GenTypeInfo,
	}
	if err := templates.Execute(f, fileTmpl, funcMap); err != nil {
		log.Fatal(err)
	}
	if err := f.Close(); err != nil {
		log.Fatal(err)
	}
}
