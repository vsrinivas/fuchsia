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
	common.GenerateOutput(WriteGoFile, goConfig)
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

func outputFileByFileName(fileName string, config common.GeneratorConfig) string {
	var err error
	relFileName, err := filepath.Rel(config.SrcRootPath(), fileName)
	base := stripExt(filepath.Base(fileName))

	if err != nil {
		log.Fatalln(err.Error())
	}
	return filepath.Join(config.OutputDir(), filepath.Dir(relFileName), base, base+".core.go")
}

// createAndOpen opens for writing the specified file. If the file or any part
// of the directory structure in its path does not exist, they are created.
func createAndOpen(outPath string) (file common.Writer) {
	// Create the directory that will contain the output.
	outDir := path.Dir(outPath)
	if err := os.MkdirAll(outDir, os.ModeDir|0777); err != nil && !os.IsExist(err) {
		log.Fatalln(err.Error())
	}

	var err error
	file, err = os.OpenFile(outPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0666)
	if err != nil {
		log.Fatalln(err.Error())
	}

	return
}

func WriteGoFile(fileName string, config common.GeneratorConfig) {
	outputFile := outputFileByFileName(fileName, config)
	writer := createAndOpen(outputFile)
	goConfig := config.(goConfig)
	fileTmpl := goConfig.translator.TranslateFidlFile(fileName)
	funcMap := template.FuncMap{
		"TypesPkg":    fileTmpl.TypesPkg,
		"DescPkg":     fileTmpl.DescPkg,
		"GenTypeInfo": config.GenTypeInfo,
	}
	writer.WriteString(templates.ExecuteTemplates(fileTmpl, funcMap))
}
