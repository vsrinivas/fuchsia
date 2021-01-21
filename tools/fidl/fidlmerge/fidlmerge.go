// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"flag"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"text/template"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var primitiveTypes = map[fidl.PrimitiveSubtype]string{
	fidl.Bool:    "bool",
	fidl.Int8:    "int8_t",
	fidl.Int16:   "int16_t",
	fidl.Int32:   "int32_t",
	fidl.Int64:   "int64_t",
	fidl.Uint8:   "uint8_t",
	fidl.Uint16:  "uint16_t",
	fidl.Uint32:  "uint32_t",
	fidl.Uint64:  "uint64_t",
	fidl.Float32: "float",
	fidl.Float64: "double",
}

func mustReadJSONIr(filename string) fidl.Root {
	root, err := fidl.ReadJSONIr(filename)
	if err != nil {
		log.Fatal(err)
	}
	return root
}

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
		cmdlineflags.FidlAmendments().Amend(mustReadJSONIr(*cmdlineflags.jsonPath)),
		cmdlineflags.outputBase,
		options)

	if results != nil {
		log.Printf("Error running generator: %v", results)
		os.Exit(1)
	}
}

// Amendments to be applied to a fidl.Root
type Amendments struct {
	ExcludedDecls []fidl.EncodedCompoundIdentifier `json:"exclusions,omitempty"`
}

func (a Amendments) Amend(root fidl.Root) fidl.Root {
	return a.ApplyExclusions(root)
}

func (a Amendments) ApplyExclusions(root fidl.Root) fidl.Root {
	if len(a.ExcludedDecls) == 0 {
		return root
	}

	excludeMap := make(map[fidl.EncodedCompoundIdentifier]struct{})
	for _, excludedDecl := range a.ExcludedDecls {
		excludeMap[excludedDecl] = struct{}{}
		delete(root.Decls, excludedDecl)
	}

	newConsts := root.Consts[:0]
	for _, element := range root.Consts {
		_, found := excludeMap[element.Name]
		if !found {
			newConsts = append(newConsts, element)
		}
	}
	root.Consts = newConsts

	newEnums := root.Enums[:0]
	for _, element := range root.Enums {
		_, found := excludeMap[element.Name]
		if !found {
			newEnums = append(newEnums, element)
		}
	}
	root.Enums = newEnums

	newProtocols := root.Protocols[:0]
	for _, element := range root.Protocols {
		_, found := excludeMap[element.Name]
		if !found {
			newProtocols = append(newProtocols, element)
		}
	}
	root.Protocols = newProtocols

	newStructs := root.Structs[:0]
	for _, element := range root.Structs {
		_, found := excludeMap[element.Name]
		if !found {
			newStructs = append(newStructs, element)
		}
	}
	root.Structs = newStructs

	newTables := root.Tables[:0]
	for _, element := range root.Tables {
		_, found := excludeMap[element.Name]
		if !found {
			newTables = append(newTables, element)
		}
	}
	root.Tables = newTables

	newUnions := root.Unions[:0]
	for _, element := range root.Unions {
		_, found := excludeMap[element.Name]
		if !found {
			newUnions = append(newUnions, element)
		}
	}
	root.Unions = newUnions

	newDeclOrder := root.DeclOrder[:0]
	for _, element := range root.DeclOrder {
		_, found := excludeMap[element]
		if !found {
			newDeclOrder = append(newDeclOrder, element)
		}
	}
	root.DeclOrder = newDeclOrder

	return root
}

// Root struct passed as the initial 'dot' for the template.
type Root struct {
	fidl.Root
	OutputBase      string
	templates       *template.Template
	options         Options
	constsByName    map[fidl.EncodedCompoundIdentifier]*fidl.Const
	enumsByName     map[fidl.EncodedCompoundIdentifier]*fidl.Enum
	protocolsByName map[fidl.EncodedCompoundIdentifier]*fidl.Protocol
	structsByName   map[fidl.EncodedCompoundIdentifier]*fidl.Struct
	tablesByName    map[fidl.EncodedCompoundIdentifier]*fidl.Table
	unionsByName    map[fidl.EncodedCompoundIdentifier]*fidl.Union
	librariesByName map[fidl.EncodedLibraryIdentifier]*fidl.Library
}

func NewRoot(ir fidl.Root, outputBase string, templates *template.Template, options Options) *Root {
	constsByName := make(map[fidl.EncodedCompoundIdentifier]*fidl.Const)
	for index, member := range ir.Consts {
		constsByName[member.Name] = &ir.Consts[index]
	}

	enumsByName := make(map[fidl.EncodedCompoundIdentifier]*fidl.Enum)
	for index, member := range ir.Enums {
		enumsByName[member.Name] = &ir.Enums[index]
	}

	protocolsByName := make(map[fidl.EncodedCompoundIdentifier]*fidl.Protocol)
	for index, member := range ir.Protocols {
		protocolsByName[member.Name] = &ir.Protocols[index]
	}

	// filter out all anonymous structs
	allStructs := ir.Structs
	ir.Structs = make([]fidl.Struct, 0)
	for _, member := range allStructs {
		if member.Anonymous {
			continue
		}
		ir.Structs = append(ir.Structs, member)
	}

	structsByName := make(map[fidl.EncodedCompoundIdentifier]*fidl.Struct)
	for index, member := range ir.Structs {
		structsByName[member.Name] = &ir.Structs[index]
	}

	tablesByName := make(map[fidl.EncodedCompoundIdentifier]*fidl.Table)
	for index, member := range ir.Tables {
		tablesByName[member.Name] = &ir.Tables[index]
	}

	unionsByName := make(map[fidl.EncodedCompoundIdentifier]*fidl.Union)
	for index, member := range ir.Unions {
		unionsByName[member.Name] = &ir.Unions[index]
	}

	librariesByName := make(map[fidl.EncodedLibraryIdentifier]*fidl.Library)
	for index, member := range ir.Libraries {
		librariesByName[member.Name] = &ir.Libraries[index]
	}

	return &Root{
		ir,
		outputBase,
		templates,
		options,
		constsByName,
		enumsByName,
		protocolsByName,
		structsByName,
		tablesByName,
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

	return "", nil
}

// Returns an output file path with the specified extension.
func (root Root) Output(ext string) string {
	return root.OutputBase + ext
}

// Gets a constant by name.
func (root Root) GetConst(name fidl.EncodedCompoundIdentifier) *fidl.Const {
	return root.constsByName[name]
}

// Gets an enum by name.
func (root Root) GetEnum(name fidl.EncodedCompoundIdentifier) *fidl.Enum {
	return root.enumsByName[name]
}

// Gets a protocol by name.
func (root Root) GetProtocol(name fidl.EncodedCompoundIdentifier) *fidl.Protocol {
	return root.protocolsByName[name]
}

// Gets a struct by name.
func (root Root) GetStruct(name fidl.EncodedCompoundIdentifier) *fidl.Struct {
	return root.structsByName[name]
}

// Gets a struct by name.
func (root Root) GetTable(name fidl.EncodedCompoundIdentifier) *fidl.Table {
	return root.tablesByName[name]
}

// Gets a union by name.
func (root Root) GetUnion(name fidl.EncodedCompoundIdentifier) *fidl.Union {
	return root.unionsByName[name]
}

// Gets a library by name.
func (root Root) GetLibrary(name fidl.EncodedLibraryIdentifier) *fidl.Library {
	return root.librariesByName[name]
}

// Generates code using the specified template.
func GenerateFidl(templatePath string, ir fidl.Root, outputBase *string, options Options) error {
	returnBytes, err := ioutil.ReadFile(templatePath)
	if err != nil {
		log.Fatalf("Error reading from %s: %v", templatePath, err)
	}

	tmpls := template.New("Templates")

	root := NewRoot(ir, *outputBase, tmpls, options)

	funcMap := template.FuncMap{
		// Gets the decltype for an EncodedCompoundIdentifier.
		"declType": func(eci fidl.EncodedCompoundIdentifier) fidl.DeclType {
			if root.Name == eci.LibraryName() {
				return root.Decls[eci]
			}
			library := root.GetLibrary(eci.LibraryName())
			return library.Decls[eci].Type
		},
		// Determines if an EncodedCompoundIdentifier refers to a local definition.
		"isLocal": func(eci fidl.EncodedCompoundIdentifier) bool {
			return root.Name == eci.LibraryName()
		},
		// Converts an identifier to snake case.
		"toSnakeCase": func(id fidl.Identifier) string {
			return fidl.ToSnakeCase(string(id))
		},
		// Converts an identifier to upper camel case.
		"toUpperCamelCase": func(id fidl.Identifier) string {
			return fidl.ToUpperCamelCase(string(id))
		},
		// Converts an identifier to lower camel case.
		"toLowerCamelCase": func(id fidl.Identifier) string {
			return fidl.ToLowerCamelCase(string(id))
		},
		// Converts an identifier to friendly case.
		"toFriendlyCase": func(id fidl.Identifier) string {
			return fidl.ToFriendlyCase(string(id))
		},
		// Removes a leading 'k' from an identifier.
		"removeLeadingK": func(id fidl.Identifier) string {
			return fidl.RemoveLeadingK(string(id))
		},
		// Gets an option value (as a string) by name.
		"getOption": func(name string) string {
			return root.options[name]
		},
		// Gets an option (as an Identifier) by name.
		"getOptionAsIdentifier": func(name string) fidl.Identifier {
			return fidl.Identifier(root.options[name])
		},
		// Gets an option (as an EncodedLibraryIdentifier) by name.
		"getOptionAsEncodedLibraryIdentifier": func(name string) fidl.EncodedLibraryIdentifier {
			return fidl.EncodedLibraryIdentifier(root.options[name])
		},
		// Gets an option (as an EncodedCompoundIdentifier) by name.
		"getOptionAsEncodedCompoundIdentifier": func(name string) fidl.EncodedCompoundIdentifier {
			return fidl.EncodedCompoundIdentifier(root.options[name])
		},
		// Returns the template executed
		"execTmpl": func(template string, data interface{}) (string, error) {
			buffer := &bytes.Buffer{}
			err = root.templates.ExecuteTemplate(buffer, template, data)
			return buffer.String(), err
		},
		// Determines if a protocol is discoverable.
		"isDiscoverable": func(i fidl.Protocol) bool {
			_, found := i.LookupAttribute("Discoverable")
			return found
		},
		// Determines if a method is transitional.
		"isTransitional": func(m fidl.Method) bool {
			_, found := m.LookupAttribute("Transitional")
			return found
		},
		// Converts a primitive subtype to its C equivalent.
		"toCType": func(p fidl.PrimitiveSubtype) string {
			return primitiveTypes[p]
		},
	}

	template.Must(tmpls.Funcs(funcMap).Parse(string(returnBytes[:])))

	err = tmpls.ExecuteTemplate(os.Stdout, "Main", root)
	if err != nil {
		return err
	}

	return nil
}
