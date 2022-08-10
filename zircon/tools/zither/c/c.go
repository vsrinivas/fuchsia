// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package c

import (
	"embed"
	"fmt"
	"path/filepath"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither"
)

//go:embed templates/*
var templates embed.FS

// Generator provides C data layout bindings.
type Generator struct {
	fidlgen.Generator
}

func NewGenerator(formatter fidlgen.Formatter) *Generator {
	gen := fidlgen.NewGenerator("CTemplates", templates, formatter, template.FuncMap{
		"PrimitiveTypeName": PrimitiveTypeName,
		"ConstName":         ConstName,
		"ConstValue":        ConstValue,
		"EnumName":          EnumName,
		"EnumMemberName":    EnumMemberName,
		"EnumMemberValue":   EnumMemberValue,
	})
	return &Generator{*gen}
}

func (gen Generator) DeclOrder() zither.DeclOrder {
	return zither.DependencyDeclOrder
}

func (gen *Generator) Generate(summary zither.Summary, outputDir string) ([]string, error) {
	parts := summary.Name.Parts()
	outputDir = filepath.Join(outputDir, filepath.Join(parts...))
	name := parts[len(parts)-1] + ".h"
	output := filepath.Join(outputDir, name)
	if err := gen.GenerateFile(output, "GenerateCFile", summary); err != nil {
		return nil, err
	}
	return []string{output}, nil
}

//
// Template functions.
//

// PrimitiveTypeName returns the C type name for a given a primitive FIDL type.
func PrimitiveTypeName(typ fidlgen.PrimitiveSubtype) string {
	switch typ {
	case fidlgen.Bool:
		return "bool"
	case fidlgen.Int8, fidlgen.Int16, fidlgen.Int32, fidlgen.Int64,
		fidlgen.Uint8, fidlgen.Uint16, fidlgen.Uint32, fidlgen.Uint64:
		return string(typ) + "_t"
	default:
		panic(fmt.Errorf("unrecognized primitive type: %s", typ))
	}
}

func nameParts(name fidlgen.Name) []string {
	return append(name.LibraryName().Parts(), name.DeclarationName())
}

// ConstName returns the name of a generated C "constant".
func ConstName(c zither.Const) string {
	parts := nameParts(c.Name)
	return fidlgen.ConstNameToAllCapsSnake(strings.Join(parts, "_"))
}

// ConstValue returns the right-hand side of a generated C "constant" declaration.
func ConstValue(c zither.Const) string {
	if c.Identifier != nil {
		switch c.Kind {
		case zither.TypeKindEnum:
			enum, member := c.Identifier.SplitMember()
			return EnumMemberName(zither.Enum{Name: enum}, zither.EnumMember{Name: member})
		case zither.TypeKindBits:
			panic("// TODO(fxbug.dev/91102): Support bits")
		default:
			return ConstName(zither.Const{Name: *c.Identifier})
		}
	}

	switch c.Kind {
	case zither.TypeKindString:
		return fmt.Sprintf("%q", c.Value)
	case zither.TypeKindBool, zither.TypeKindInteger:
		fidlType := fidlgen.PrimitiveSubtype(c.Type)
		cType := PrimitiveTypeName(fidlType)
		val := c.Value
		if c.Kind == zither.TypeKindInteger {
			// In the case of an signed type, the unsigned-ness will be
			// explicitly cast away.
			val += "u"
		}
		return fmt.Sprintf("((%s)(%s))", cType, val)
	case zither.TypeKindEnum:
		// Enum constants should have been handled above.
		panic(fmt.Sprintf("enum constants must be given by an identifier: %#v", c))
	case zither.TypeKindBits:
		panic("TODO(fxbug.dev/51002): Support bits")
	default:
		panic(fmt.Sprintf("%s has unknown constant kind: %s", c.Name, c.Type))
	}
}

// EnumName returns the type name of a generated C "enum".
func EnumName(enum zither.Enum) string {
	parts := nameParts(enum.Name)
	return fidlgen.ToSnakeCase(strings.Join(parts, "_")) + "_t"
}

// EnumMemberName returns the name of a generated C "enum" member.
func EnumMemberName(enum zither.Enum, member zither.EnumMember) string {
	parts := append(nameParts(enum.Name), member.Name)
	return fidlgen.ConstNameToAllCapsSnake(strings.Join(parts, "_"))
}

// EnumMemberValue returns the value of a generated C "enum" member.
func EnumMemberValue(enum zither.Enum, member zither.EnumMember) string {
	return fmt.Sprintf("((%s)(%su))", EnumName(enum), member.Value)
}
