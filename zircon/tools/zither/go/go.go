// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"embed"
	"fmt"
	"path/filepath"
	"strconv"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither"
)

//go:embed templates/*
var templates embed.FS

// Generator provides go data layout bindings.
type Generator struct {
	fidlgen.Generator
}

func NewGenerator(formatter fidlgen.Formatter) *Generator {
	gen := fidlgen.NewGenerator("GoTemplates", templates, formatter, template.FuncMap{
		"PackageBasename":  PackageBasename,
		"ConstName":        ConstName,
		"ConstType":        ConstType,
		"ConstValue":       ConstValue,
		"EnumName":         EnumName,
		"EnumMemberName":   EnumMemberName,
		"BitsName":         BitsName,
		"BitsMemberName":   BitsMemberName,
		"StructName":       StructName,
		"StructMemberName": StructMemberName,
		"StructMemberType": StructMemberType,
	})
	return &Generator{*gen}
}

func (gen Generator) DeclOrder() zither.DeclOrder {
	// Go enforces no parsing order for declarations.
	return zither.SourceDeclOrder
}

func (gen *Generator) Generate(summaries []zither.FileSummary, outputDir string) ([]string, error) {
	libParts := summaries[0].Library.Parts()
	libPath := filepath.Join(libParts...)
	outputDir = filepath.Join(outputDir, libPath)

	var outputs []string

	// Generate a file containing the package's name
	pkgName := filepath.Join(outputDir, "pkg_name.txt")
	if err := fidlgen.WriteFileIfChanged(pkgName, []byte(libPath)); err != nil {
		return nil, err
	}
	outputs = append(outputs, pkgName)

	for _, summary := range summaries {
		output := filepath.Join(outputDir, summary.Name+".go")
		if err := gen.GenerateFile(output, "GenerateGoFile", summary); err != nil {
			return nil, err
		}
		outputs = append(outputs, output)
	}
	return outputs, nil
}

//
// Template functions.
//

func PackageBasename(lib fidlgen.LibraryName) string {
	parts := lib.Parts()
	return parts[len(parts)-1]
}

func getName(name fidlgen.Name) string {
	return fidlgen.ToUpperCamelCase(name.DeclarationName())
}

func ConstName(c zither.Const) string {
	return getName(c.Name)
}

func ConstType(c zither.Const) string {
	switch c.Kind {
	case zither.TypeKindBool, zither.TypeKindInteger, zither.TypeKindString:
		return c.Type
	case zither.TypeKindEnum, zither.TypeKindBits:
		return fidlgen.MustReadName(c.Type).DeclarationName()
	default:
		panic(fmt.Sprintf("%s has unknown constant kind: %s", c.Name, c.Type))
	}
}

func ConstValue(c zither.Const) string {
	if c.Identifier != nil {
		switch c.Kind {
		case zither.TypeKindEnum:
			enum, member := c.Identifier.SplitMember()
			return EnumMemberName(zither.Enum{Name: enum}, zither.EnumMember{Name: member})
		case zither.TypeKindBits:
			bits, member := c.Identifier.SplitMember()
			return BitsMemberName(zither.Bits{Name: bits}, zither.BitsMember{Name: member})
		default:
			return getName(*c.Identifier)
		}
	}

	switch c.Kind {
	case zither.TypeKindString:
		return fmt.Sprintf("%q", c.Value)
	case zither.TypeKindBool, zither.TypeKindInteger:
		return c.Value
	case zither.TypeKindEnum:
		// Enum constants should have been handled above.
		panic(fmt.Sprintf("enum constants must be given by an identifier: %#v", c))
	case zither.TypeKindBits:
		val, err := strconv.Atoi(c.Value)
		if err != nil {
			panic(fmt.Sprintf("%s has malformed integral value: %s", c.Name, err))
		}
		return fmt.Sprintf("%#b", val)
	default:
		panic(fmt.Sprintf("%s has unknown constant kind: %s", c.Name, c.Type))
	}
}

func EnumName(enum zither.Enum) string {
	return getName(enum.Name)
}

func EnumMemberName(enum zither.Enum, member zither.EnumMember) string {
	return getName(enum.Name) + fidlgen.ToUpperCamelCase(member.Name)
}

func BitsName(bits zither.Bits) string {
	return getName(bits.Name)
}

func BitsMemberName(bits zither.Bits, member zither.BitsMember) string {
	return getName(bits.Name) + fidlgen.ToUpperCamelCase(member.Name)
}

func StructName(s zither.Struct) string {
	return getName(s.Name)
}

func StructMemberName(member zither.StructMember) string {
	return fidlgen.ToUpperCamelCase(member.Name)
}

func StructMemberType(member zither.StructMember) string {
	return structMemberType(member.Type)
}

func structMemberType(desc zither.TypeDescriptor) string {
	switch desc.Kind {
	case zither.TypeKindBool, zither.TypeKindInteger:
		return desc.Type
	case zither.TypeKindEnum, zither.TypeKindBits, zither.TypeKindStruct:
		layout, _ := fidlgen.MustReadName(desc.Type).SplitMember()
		return layout.DeclarationName()
	case zither.TypeKindArray:
		return fmt.Sprintf("[%d]", *desc.ElementCount) + structMemberType(*desc.ElementType)
	default:
		panic(fmt.Sprintf("unsupported type kind: %v", desc.Kind))
	}
}
