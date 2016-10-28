// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This program reads from standard input a serialized MojomFileGraph
// (See mojo/public/interfaces/bindings/mojom_files.mojom) as output by the
// mojom parser and outputs a list of types and constants defined in the files
// in the file graph. The output for each file is located in a directory
// structure based on the file's module namespace and in a file named after the
// processed file.
// For example, for file foo.mojom with module namespace abc.bcd, the output
// will be located in abc/bcd/foo.mojom.sample.
//
// This program is intended as an example for those who would want
// to use the output of the mojom parser such as code generator authors.

package main

import (
	"bufio"
	"io/ioutil"
	"log"
	"os"
	"path"
	"strings"
	"text/template"

	"mojo/public/go/bindings"
	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

func main() {
	inputBytes, err := ioutil.ReadAll(os.Stdin)
	if err != nil {
		log.Fatalf("failed to read: %v", err)
	}

	decoder := bindings.NewDecoder(inputBytes, nil)
	fileGraph := mojom_files.MojomFileGraph{}
	if err := fileGraph.Decode(decoder); err != nil {
		log.Fatalf("Failed to decode file graph: %v", err)
	}

	for fileKey := range fileGraph.Files {
		printFile(fileGraph, fileKey)
	}
}

type fileDataStruct struct {
	FileName   string
	Namespace  string
	Imports    []string
	Interfaces []string
	Structs    []string
	Unions     []string
	Enums      []string
	Constants  []string
}

// TODO(azani): Use whitespace trimming features when it comes out on 2016-02-01.
// https://github.com/golang/go/issues/9969
const templateText = `File name: {{.FileName}}
Namespace: {{.Namespace}}

Imports:
{{range .Imports}}
{{.}}
{{end}}

Declared Objects:
{{range .Interfaces}}interface {{.}}
{{end}}
{{range .Structs}}struct {{.}}
{{end}}
{{range .Unions}}union {{.}}
{{end}}
{{range .Enums}}enum {{.}}
{{end}}
{{range .Constants}}const {{.}}
{{end}}
`

// printFile accepts a parsed mojom file graph and a key into fileGraph.Files.
// It creates a file in the appropriate directory structure, based upon the
// processed file's name and module namespace and prints to that file a
// representation of the objects defined in the file in question.
func printFile(fileGraph mojom_files.MojomFileGraph, fileKey string) {
	file := fileGraph.Files[fileKey]
	outPathDir := strings.Replace(*file.ModuleNamespace, ".", "/", -1)
	if err := os.MkdirAll(outPathDir, os.ModeDir|0777); err != nil {
		log.Fatalf("Could not create %v: %v", outPathDir, err)
	}

	outFilePath := path.Join(outPathDir, path.Base(file.FileName)+".sample")
	outFile, err := os.Create(outFilePath)
	if err != nil {
		log.Fatalf("Could not open %v: %v", outFilePath, err)
	}

	defer outFile.Close()
	outFileBuf := bufio.NewWriter(outFile)
	tmpl, err := template.New("generator_template").Parse(templateText)
	if err != nil {
		log.Fatalf("Could not parse template: %v", err)
	}

	fileData := getFileData(fileGraph, fileKey)
	if err := tmpl.Execute(outFileBuf, fileData); err != nil {
		log.Fatalf("Error during template execution: %v", err)
	}

	if err := outFileBuf.Flush(); err != nil {
		log.Fatalf("Could not flush to file: %v", err)
	}
}

// getFileData accepts a parsed mojom file graph and a key into fileGraph.Files.
// It extracts from that file the data needed to generate the output of this
// program and stores it in a fileDataStruct.
func getFileData(fileGraph mojom_files.MojomFileGraph, fileKey string) fileDataStruct {
	file := fileGraph.Files[fileKey]

	fileData := fileDataStruct{}
	fileData.FileName = file.FileName
	fileData.Namespace = *file.ModuleNamespace

	if file.Imports != nil {
		fileData.Imports = make([]string, len(*file.Imports))
		for index, importKey := range *file.Imports {
			fileData.Imports[index] = fileGraph.Files[importKey].FileName
		}
	}

	fileData.Interfaces = getTypes(fileGraph, file.DeclaredMojomObjects.Interfaces)
	fileData.Structs = getTypes(fileGraph, file.DeclaredMojomObjects.Structs)
	fileData.Unions = getTypes(fileGraph, file.DeclaredMojomObjects.Unions)
	fileData.Enums = getTypes(fileGraph, file.DeclaredMojomObjects.TopLevelEnums)
	fileData.Constants = getConstants(fileGraph, file.DeclaredMojomObjects.TopLevelConstants)
	return fileData
}

// getTypes accepts a parsed mojom file graph and a list of keys into
// fileGraph.ResolvedTypes. It creates a list of the names of all the
// corresponding types and returns that list.
func getTypes(fileGraph mojom_files.MojomFileGraph, typeKeys *[]string) []string {
	if typeKeys == nil {
		return []string{}
	}

	typeNames := make([]string, len(*typeKeys))

	for index, typeKey := range *typeKeys {
		userDefinedType := fileGraph.ResolvedTypes[typeKey]
		switch mojomType := userDefinedType.Interface().(type) {
		case mojom_types.MojomInterface:
			typeNames[index] = *mojomType.DeclData.ShortName
		case mojom_types.MojomStruct:
			typeNames[index] = *mojomType.DeclData.ShortName
		case mojom_types.MojomEnum:
			typeNames[index] = *mojomType.DeclData.ShortName
		case mojom_types.MojomUnion:
			typeNames[index] = *mojomType.DeclData.ShortName
		}
	}

	return typeNames
}

// getConstants accepts a parsed mojom file graph and a list of keys into
// fileGraph.ResolvedValues. It creates a list of the names of all the
// corresponding constants and returns that list.
func getConstants(fileGraph mojom_files.MojomFileGraph, constKeys *[]string) []string {
	if constKeys == nil {
		return []string{}
	}

	constNames := make([]string, len(*constKeys))

	for index, constKey := range *constKeys {
		userDefinedValue := fileGraph.ResolvedValues[constKey]
		declaredConstant := userDefinedValue.Interface().(mojom_types.DeclaredConstant)
		constNames[index] = *declaredConstant.DeclData.ShortName
	}

	return constNames
}
