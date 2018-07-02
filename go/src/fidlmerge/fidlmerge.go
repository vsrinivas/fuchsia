// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/compiler/backend/common"
	"fidl/compiler/backend/types"
	"flag"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"text/template"
)

func main() {
	cmdlineflags := GetFlags()
	options := make(Options)
	flag.Var(&options, "options",
		"comma-separated list if name=value pairs.")
	flag.Parse()

	if !flag.Parsed() || !cmdlineflags.Valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	results := GenerateFidl(*cmdlineflags.templatePath,
		cmdlineflags.FidlTypes(),
		cmdlineflags.outputBase,
		options)

	if results != nil {
		log.Printf("Error running generator: %v", results)
		os.Exit(1)
	}
}

// Root struct passed as the initial 'dot' for the template.
type Root struct {
	types.Root
	OutputBase       string
	templates        *template.Template
	options          Options
	constsByName     map[types.EncodedCompoundIdentifier]*types.Const
	enumsByName      map[types.EncodedCompoundIdentifier]*types.Enum
	interfacesByName map[types.EncodedCompoundIdentifier]*types.Interface
	structsByName    map[types.EncodedCompoundIdentifier]*types.Struct
	unionsByName     map[types.EncodedCompoundIdentifier]*types.Union
	librariesByName  map[types.EncodedLibraryIdentifier]*types.Library
}

func NewRoot(fidl types.Root, outputBase string, templates *template.Template, options Options) *Root {
	constsByName := make(map[types.EncodedCompoundIdentifier]*types.Const)
	for index, member := range fidl.Consts {
		constsByName[member.Name] = &fidl.Consts[index]
	}

	enumsByName := make(map[types.EncodedCompoundIdentifier]*types.Enum)
	for index, member := range fidl.Enums {
		enumsByName[member.Name] = &fidl.Enums[index]
	}

	interfacesByName := make(map[types.EncodedCompoundIdentifier]*types.Interface)
	for index, member := range fidl.Interfaces {
		interfacesByName[member.Name] = &fidl.Interfaces[index]
	}

	structsByName := make(map[types.EncodedCompoundIdentifier]*types.Struct)
	for index, member := range fidl.Structs {
		structsByName[member.Name] = &fidl.Structs[index]
	}

	unionsByName := make(map[types.EncodedCompoundIdentifier]*types.Union)
	for index, member := range fidl.Unions {
		unionsByName[member.Name] = &fidl.Unions[index]
	}

	librariesByName := make(map[types.EncodedLibraryIdentifier]*types.Library)
	for index, member := range fidl.Libraries {
		librariesByName[member.Name] = &fidl.Libraries[index]
	}

	return &Root{
		fidl,
		outputBase,
		templates,
		options,
		constsByName,
		enumsByName,
		interfacesByName,
		structsByName,
		unionsByName,
		librariesByName,
	}
}

// Applies the specified template to the specified data and writes the output
// to outputPath.
func (root Root) Generate(outputPath string, template string, data interface{}) (string, error) {
	if err := os.MkdirAll(filepath.Dir(outputPath), os.ModePerm); err != nil {
		return "", err
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return "", err
	}
	defer f.Close()

	err = root.templates.ExecuteTemplate(f, template, data)
	if err != nil {
		return "", err
	}

	return outputPath, nil
}

// Returns an output file path with the specified extension.
func (root Root) Output(ext string) string {
	return root.OutputBase + ext
}

// Gets a constant by name.
func (root Root) GetConst(name types.EncodedCompoundIdentifier) *types.Const {
	return root.constsByName[name]
}

// Gets an enum by name.
func (root Root) GetEnum(name types.EncodedCompoundIdentifier) *types.Enum {
	return root.enumsByName[name]
}

// Gets a interface by name.
func (root Root) GetInterface(name types.EncodedCompoundIdentifier) *types.Interface {
	return root.interfacesByName[name]
}

// Gets a struct by name.
func (root Root) GetStruct(name types.EncodedCompoundIdentifier) *types.Struct {
	return root.structsByName[name]
}

// Gets a union by name.
func (root Root) GetUnion(name types.EncodedCompoundIdentifier) *types.Union {
	return root.unionsByName[name]
}

// Gets a library by name.
func (root Root) GetLibrary(name types.EncodedLibraryIdentifier) *types.Library {
	return root.librariesByName[name]
}

// Generates code using the specified template.
func GenerateFidl(templatePath string, fidl types.Root, outputBase *string, options Options) error {
	bytes, err := ioutil.ReadFile(templatePath)
	if err != nil {
		log.Fatalf("Error reading from %s: %v", templatePath, err)
	}

	tmpls := template.New("Templates")

	root := NewRoot(fidl, *outputBase, tmpls, options)

	funcMap := template.FuncMap{
		// Gets the decltype for an EncodedCompoundIdentifier.
		"declType": func(eci types.EncodedCompoundIdentifier) types.DeclType {
			library := root.GetLibrary(eci.LibraryName())
			return library.Decls[eci]
		},
		// Determines if an EncodedCompoundIdentifier refers to a local definition.
		"isLocal": func(eci types.EncodedCompoundIdentifier) bool {
			return root.Name == eci.LibraryName()
		},
		// Converts an identifier to snake case.
		"toSnakeCase": func(id types.Identifier) string {
			return common.ToSnakeCase(string(id))
		},
		// Converts an identifier to upper camel case.
		"toUpperCamelCase": func(id types.Identifier) string {
			return common.ToUpperCamelCase(string(id))
		},
		// Converts an identifier to lower camel case.
		"toLowerCamelCase": func(id types.Identifier) string {
			return common.ToLowerCamelCase(string(id))
		},
		// Converts an identifier to friendly case.
		"toFriendlyCase": func(id types.Identifier) string {
			return common.ToFriendlyCase(string(id))
		},
		// Removes a leading 'k' from an identifier.
		"removeLeadingK": func(id types.Identifier) string {
			return common.RemoveLeadingK(string(id))
		},
		// Gets an option value (as a string) by name.
		"getOption": func(name string) string {
			return root.options[name]
		},
		// Gets an option (as an Identifier) by name.
		"getOptionAsIdentifier": func(name string) types.Identifier {
			return types.Identifier(root.options[name])
		},
		// Gets an option (as an EncodedLibraryIdentifier) by name.
		"getOptionAsEncodedLibraryIdentifier": func(name string) types.EncodedLibraryIdentifier {
			return types.EncodedLibraryIdentifier(root.options[name])
		},
		// Gets an option (as an EncodedCompoundIdentifier) by name.
		"getOptionAsEncodedCompoundIdentifier": func(name string) types.EncodedCompoundIdentifier {
			return types.EncodedCompoundIdentifier(root.options[name])
		},
	}

	template.Must(tmpls.Funcs(funcMap).Parse(string(bytes[:])))

	err = tmpls.ExecuteTemplate(os.Stdout, "Main", root)
	if err != nil {
		return err
	}

	return nil
}
