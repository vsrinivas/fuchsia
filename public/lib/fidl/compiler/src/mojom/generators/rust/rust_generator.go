// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"path"
	"path/filepath"
	"os"
	"text/template"

	"mojom/generators/rust/rustgen"
	"mojom/generators/rust/templates"
	"mojom/generators/common"
)

func main() {
	tmpls := template.New("RustTemplates")
	template.Must(tmpls.Parse(templates.GenerateSourceFile))
	template.Must(tmpls.Parse(templates.GenerateEnum))
	template.Must(tmpls.Parse(templates.GenerateStruct))
	template.Must(tmpls.Parse(templates.GenerateUnion))
	template.Must(tmpls.Parse(templates.GenerateEndpoint))
	template.Must(tmpls.Parse(templates.GenerateInterface))
	template.Must(tmpls.Parse(templates.GenerateModFile))

	modules := rustgen.NewModule()
	config := common.GetCliConfig(os.Args)
	names := make(map[string]*rustgen.Names)
	for name, file := range config.FileGraph().Files {
		names[name] = rustgen.CollectAllNames(config.FileGraph(), &file)
		names[name].MangleKeywords()
	}
	common.GenerateOutput(func(fileName string, config common.GeneratorConfig) {
		mojomFile := config.FileGraph().Files[fileName]
		namespace := rustgen.GetNamespace(&mojomFile)
		outputFile := outputFileByFileName(fileName, namespace, config.OutputDir())
		modules.Add(namespace, rustgen.RustModuleName(fileName))
		sourceWriter := createAndOpen(outputFile)
		context := rustgen.Context{
			FileGraph: config.FileGraph(),
			File: &mojomFile,
			RustNames: &names,
		}
		sourceInfo := rustgen.NewSourceTemplate(&context, config.SrcRootPath())
		if err := tmpls.ExecuteTemplate(sourceWriter, "GenerateSourceFile", sourceInfo); err != nil {
			log.Fatal(err)
		}
		log.Printf("Processed %s", fileName)
	}, config)

	createModules(modules, config.OutputDir(), tmpls)
}



// Figures out what the output file location will be for a mojom file
// given the output directory and its namespace.
func outputFileByFileName(fileName string, namespace []string, outputDir string) string {
	base := rustgen.RustModuleName(fileName)
	path := outputDir
	for _, module := range namespace {
		path = filepath.Join(path, module)
	}
	return filepath.Join(path, base + ".rs")
}

// Uses templates to actually create the module files recursively
func createModules(root *rustgen.Module, outputDir string, tmpls *template.Template) {
	writer := createAndOpen(filepath.Join(outputDir, "mod.rs"))
	if err := tmpls.ExecuteTemplate(writer, "GenerateModFile", rustgen.NewModuleTemplate(root)); err != nil {
		log.Fatal(err)
	}
	for name, module := range root.Submodules {
		path := filepath.Join(outputDir, name)
		createModules(module, path, tmpls)
	}
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

