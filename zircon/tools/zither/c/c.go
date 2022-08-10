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
		"ConstName":  ConstName,
		"ConstValue": ConstValue,
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

// primitiveTypeName returns the C type name for a given a primitive FIDL type.
func primitiveTypeName(typ fidlgen.PrimitiveSubtype) string {
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

// ConstName returns the name of a generated C "constant".
func ConstName(c zither.Const) string {
	parts := append(c.Name.LibraryName().Parts(), c.Name.DeclarationName())
	return fidlgen.ConstNameToAllCapsSnake(strings.Join(parts, "_"))
}

// ConstValue returns the right-hand side of a generated C "constant" declaration.
func ConstValue(c zither.Const) string {
	if c.Identifier != nil {
		return ConstName(zither.Const{Name: *c.Identifier})
	}

	switch c.Kind {
	case zither.TypeKindString:
		return fmt.Sprintf("%q", c.Value)
	case zither.TypeKindBool, zither.TypeKindInteger:
		fidlType := fidlgen.PrimitiveSubtype(c.Type)
		cType := primitiveTypeName(fidlType)
		val := c.Value
		if c.Kind == zither.TypeKindInteger {
			// In the case of an signed type, the unsigned-ness will be
			// explicitly cast away.
			val += "u"
		}
		return fmt.Sprintf("((%s)(%s))", cType, val)
	case zither.TypeKindEnum:
		panic("TODO(fxbug.dev/51002): Support enums")
	case zither.TypeKindBits:
		panic("TODO(fxbug.dev/51002): Support bits")
	default:
		panic(fmt.Sprintf("unknown constant kind: %s", c.Type))
	}
}
