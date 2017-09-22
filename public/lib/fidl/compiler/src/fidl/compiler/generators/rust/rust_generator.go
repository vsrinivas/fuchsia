// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"log"
	"os"
	"path"
	"path/filepath"
	"strings"
	"text/template"

	"fidl/compiler/generators/common"
	"fidl/compiler/generators/rust/rustgen"
	"fidl/compiler/generators/rust/templates"
)

func main() {
	tmpls := template.New("RustTemplates")
	template.Must(tmpls.Parse(templates.GenerateSourceFile))
	template.Must(tmpls.Parse(templates.GenerateEnum))
	template.Must(tmpls.Parse(templates.GenerateStruct))
	template.Must(tmpls.Parse(templates.GenerateUnion))
	template.Must(tmpls.Parse(templates.GenerateEndpoint))
	template.Must(tmpls.Parse(templates.GenerateInterface))

	config := common.GetCliConfig(os.Args)
	names := make(map[string]*rustgen.Names)
	for name, file := range config.FileGraph().Files {
		names[name] = rustgen.CollectAllNames(config.FileGraph(), &file)
	}
	var mapEntries map[string]string
	var err error
	if config.DependencyMapFile() != "" {
		mapEntries, err = readMap(config.DependencyMapFile())
		if err != nil {
			log.Fatal(err)
		}
	}
	common.GenerateOutput(func(fileName string, config common.GeneratorConfig) {
		mojomFile := config.FileGraph().Files[fileName]
		namespace := rustgen.GetNamespace(&mojomFile)
		outputFile := outputFileByFileName(fileName, namespace, config)
		sourceWriter := createAndOpen(outputFile)
		context := rustgen.Context{
			FileGraph:   config.FileGraph(),
			File:        &mojomFile,
			RustNames:   &names,
			Map:         &mapEntries,
			SrcRootPath: config.SrcRootPath(),
		}
		sourceInfo := rustgen.NewSourceTemplate(&context)
		if err := tmpls.ExecuteTemplate(sourceWriter, "GenerateSourceFile", sourceInfo); err != nil {
			log.Fatal(err)
		}
		log.Printf("Processed %s", fileName)
	}, config)

}

func stripExt(fileName string) string {
	return strings.TrimSuffix(fileName, filepath.Ext(fileName))
}

// Figures out what the output file location will be for a mojom file
// given the output directory and its namespace.
func outputFileByFileName(fileName string, namespace []string, config common.GeneratorConfig) string {
	relFileName, err := filepath.Rel(config.SrcRootPath(), fileName)
	if err != nil {
		log.Fatal(err)
	}
	base := stripExt(filepath.Base(fileName))
	return filepath.Join(config.OutputDir(), filepath.Dir(relFileName), base+".rs")
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

// convert a GN-style label (ex //garnet/foo:services) to a crate name
// (ex garnet_foo_services).
func labelToCrate(label string) string {
	split_target := strings.Split(label, ":")
	parts := strings.Split(split_target[0], "/")
	for len(parts) != 0 && parts[0] == "" {
		parts = parts[1:]
	}
	if split_target[1] != parts[len(parts)-1] {
		parts = append(parts, split_target[1])
	}
	return strings.Join(parts, "_")
}

func labelsToCrates(labels []string) string {
	crates := make([]string, len(labels))
	for i, label := range labels {
		crates[i] = labelToCrate(label)
	}
	return strings.Join(crates, "::")
}

func chooseBest(crates1 string, crates2 string) string {
	if len(crates1) < len(crates2) {
		return crates1
	}
	if len(crates2) < len(crates1) {
		return crates2
	}
	if crates1 < crates2 {
		return crates1
	}
	return crates2
}

func readMap(path string) (map[string]string, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	result := make(map[string]string)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		toks := strings.Split(scanner.Text(), " ")
		if toks[0] == "pub" {
			toks = toks[1:]
		}
		path := strings.TrimRight(toks[0], ":")
		// skip self label (toks[1])
		crates := labelsToCrates(toks[2:])
		if _, present := result[path]; present {
			result[path] = chooseBest(result[path], crates)
		} else {
			result[path] = crates
		}
	}
	return result, scanner.Err()
}
