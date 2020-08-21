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

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "bool",
	types.Int8:    "int8_t",
	types.Int16:   "int16_t",
	types.Int32:   "int32_t",
	types.Int64:   "int64_t",
	types.Uint8:   "uint8_t",
	types.Uint16:  "uint16_t",
	types.Uint32:  "uint32_t",
	types.Uint64:  "uint64_t",
	types.Float32: "float",
	types.Float64: "double",
}

func mustReadJSONIr(filename string) types.Root {
	root, err := types.ReadJSONIr(filename)
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

// Amendments to be applied to a types.Root
type Amendments struct {
	ExcludedDecls []types.EncodedCompoundIdentifier `json:"exclusions,omitempty"`
}

func (a Amendments) Amend(root types.Root) types.Root {
	return a.ApplyExclusions(root)
}

func (a Amendments) ApplyExclusions(root types.Root) types.Root {
	if len(a.ExcludedDecls) == 0 {
		return root
	}

	excludeMap := make(map[types.EncodedCompoundIdentifier]struct{})
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
	types.Root
	OutputBase      string
	templates       *template.Template
	options         Options
	constsByName    map[types.EncodedCompoundIdentifier]*types.Const
	enumsByName     map[types.EncodedCompoundIdentifier]*types.Enum
	protocolsByName map[types.EncodedCompoundIdentifier]*types.Protocol
	structsByName   map[types.EncodedCompoundIdentifier]*types.Struct
	tablesByName    map[types.EncodedCompoundIdentifier]*types.Table
	unionsByName    map[types.EncodedCompoundIdentifier]*types.Union
	librariesByName map[types.EncodedLibraryIdentifier]*types.Library
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

	protocolsByName := make(map[types.EncodedCompoundIdentifier]*types.Protocol)
	for index, member := range fidl.Protocols {
		protocolsByName[member.Name] = &fidl.Protocols[index]
	}

	// filter out all anonymous structs
	allStructs := fidl.Structs
	fidl.Structs = make([]types.Struct, 0)
	for _, member := range allStructs {
		if member.Anonymous {
			continue
		}
		fidl.Structs = append(fidl.Structs, member)
	}

	structsByName := make(map[types.EncodedCompoundIdentifier]*types.Struct)
	for index, member := range fidl.Structs {
		structsByName[member.Name] = &fidl.Structs[index]
	}

	tablesByName := make(map[types.EncodedCompoundIdentifier]*types.Table)
	for index, member := range fidl.Tables {
		tablesByName[member.Name] = &fidl.Tables[index]
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
func (root Root) GetConst(name types.EncodedCompoundIdentifier) *types.Const {
	return root.constsByName[name]
}

// Gets an enum by name.
func (root Root) GetEnum(name types.EncodedCompoundIdentifier) *types.Enum {
	return root.enumsByName[name]
}

// Gets a protocol by name.
func (root Root) GetProtocol(name types.EncodedCompoundIdentifier) *types.Protocol {
	return root.protocolsByName[name]
}

// Gets a struct by name.
func (root Root) GetStruct(name types.EncodedCompoundIdentifier) *types.Struct {
	return root.structsByName[name]
}

// Gets a struct by name.
func (root Root) GetTable(name types.EncodedCompoundIdentifier) *types.Table {
	return root.tablesByName[name]
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
	returnBytes, err := ioutil.ReadFile(templatePath)
	if err != nil {
		log.Fatalf("Error reading from %s: %v", templatePath, err)
	}

	tmpls := template.New("Templates")

	root := NewRoot(fidl, *outputBase, tmpls, options)

	funcMap := template.FuncMap{
		// Gets the decltype for an EncodedCompoundIdentifier.
		"declType": func(eci types.EncodedCompoundIdentifier) types.DeclType {
			if root.Name == eci.LibraryName() {
				return root.Decls[eci]
			}
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
		// Returns the template executed
		"execTmpl": func(template string, data interface{}) (string, error) {
			buffer := &bytes.Buffer{}
			err = root.templates.ExecuteTemplate(buffer, template, data)
			return buffer.String(), err
		},
		// Determines if a protocol is discoverable.
		"isDiscoverable": func(i types.Protocol) bool {
			_, found := i.LookupAttribute("Discoverable")
			return found
		},
		// Determines if a method is transitional.
		"isTransitional": func(m types.Method) bool {
			_, found := m.LookupAttribute("Transitional")
			return found
		},
		// Converts a primitive subtype to its C equivalent.
		"toCType": func(p types.PrimitiveSubtype) string {
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
