// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dynfidl

import (
	"bytes"
	"fmt"
	"strconv"
	"strings"
	"text/template"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidllibrust "go.fuchsia.dev/fuchsia/tools/fidl/gidl/librust"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var conformanceTmpl = template.Must(template.New("conformanceTmpls").Parse(`
use dynfidl::{BasicField, Field, Structure, VectorField};

{{ range .EncodeSuccessCases }}
#[test]
fn test_{{ .Name }}_encode() {
	let value = {{ .Value }};
	let mut buf = vec![];
	value.encode(&mut buf);
	assert_eq!(
		buf,
		{{ .Bytes }},
		"observed (left) must match expected (right)",
	);
}
{{ end }}
`))

type conformanceTmplInput struct {
	EncodeSuccessCases []encodeSuccessCase
}

type encodeSuccessCase struct {
	Name, Value, Bytes string
}

func GenerateConformanceTests(gidl gidlir.All, fidl fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)

	// dynfidl only supports encode tests (it's an encoder)
	encodeSuccessCases, err := encodeSuccessCases(gidl.EncodeSuccess, schema)
	if err != nil {
		return nil, err
	}

	input := conformanceTmplInput{
		EncodeSuccessCases: encodeSuccessCases,
	}
	var buf bytes.Buffer
	err = conformanceTmpl.Execute(&buf, input)
	return buf.Bytes(), err
}

func encodeSuccessCases(gidlEncodeSuccesses []gidlir.EncodeSuccess, schema gidlmixer.Schema) ([]encodeSuccessCase, error) {
	var encodeSuccessCases []encodeSuccessCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		decl, err := schema.ExtractDeclarationEncodeSuccess(encodeSuccess.Value, encodeSuccess.HandleDefs)
		if err != nil {
			return nil, fmt.Errorf("encode success %s: %s", encodeSuccess.Name, err)
		}
		if !isSupported(decl) {
			continue
		}
		visited := visit(encodeSuccess.Value, decl)
		for _, encoding := range encodeSuccess.Encodings {
			name := fidlgen.ToSnakeCase(fmt.Sprintf("%s_%s", encodeSuccess.Name, encoding.WireFormat))
			encodeSuccessCases = append(encodeSuccessCases, encodeSuccessCase{
				Name:  name,
				Value: visited.ValueStr,
				Bytes: gidllibrust.BuildBytes(encoding.Bytes),
			})
		}
	}
	return encodeSuccessCases, nil
}

// check whether dynfidl supports the layout specified by the decl
func isSupported(decl gidlmixer.Declaration) bool {
	if decl.IsNullable() {
		return false
	}

	switch decl := decl.(type) {
	case *gidlmixer.StructDecl:
		if decl.IsResourceType() {
			return false
		}
		for _, fieldName := range decl.FieldNames() {
			fieldDecl, _ := decl.Field(fieldName)
			if !isSupportedStructField(fieldDecl) {
				return false
			}
		}
		return true
	case *gidlmixer.ArrayDecl, *gidlmixer.BitsDecl, *gidlmixer.BoolDecl, *gidlmixer.EnumDecl,
		*gidlmixer.FloatDecl, *gidlmixer.HandleDecl, *gidlmixer.IntegerDecl, *gidlmixer.StringDecl,
		*gidlmixer.TableDecl, *gidlmixer.UnionDecl, *gidlmixer.VectorDecl:
		return false
	default:
		panic(fmt.Sprint("unrecognized type %s", decl))
	}
}

// check whether dynfidl supports the layout specified by the decl as the field of a struct
func isSupportedStructField(decl gidlmixer.Declaration) bool {
	if decl.IsNullable() {
		return false
	}

	switch decl := decl.(type) {
	case *gidlmixer.BoolDecl, *gidlmixer.IntegerDecl, *gidlmixer.StringDecl:
		return true
	case *gidlmixer.VectorDecl:
		return isSupportedVectorElement(decl.Elem())
	case *gidlmixer.ArrayDecl, *gidlmixer.BitsDecl, *gidlmixer.EnumDecl, *gidlmixer.FloatDecl,
		*gidlmixer.HandleDecl, *gidlmixer.StructDecl, *gidlmixer.TableDecl, *gidlmixer.UnionDecl:
		return false
	default:
		panic(fmt.Sprintf("unrecognized type %s", decl))
	}
}

// check whether dynfidl supports the layout specified by the decl as an element in a vector
func isSupportedVectorElement(decl gidlmixer.Declaration) bool {
	if decl.IsNullable() {
		return false
	}

	switch decl := decl.(type) {
	case *gidlmixer.BoolDecl, *gidlmixer.IntegerDecl, *gidlmixer.StringDecl:
		return true
	case *gidlmixer.VectorDecl:
		// dynfidl only supports vectors-of-vectors-of-bytes
		switch decl := decl.Elem().(type) {
		case gidlmixer.PrimitiveDeclaration:
			switch decl.Subtype() {
			case fidlgen.Uint8:
				return true
			default:
				return false
			}
		default:
			return false
		}
	case *gidlmixer.ArrayDecl, *gidlmixer.BitsDecl, *gidlmixer.EnumDecl, *gidlmixer.FloatDecl,
		*gidlmixer.HandleDecl, *gidlmixer.StructDecl, *gidlmixer.TableDecl, *gidlmixer.UnionDecl:
		return false
	default:
		panic(fmt.Sprintf("unrecognized type %s", decl))
	}
}

type visitResult struct {
	ValueStr            string
	OuterVariant        outerVariant
	InnerEnumAndVariant string
}

type outerVariant int

const (
	_ outerVariant = iota
	basicVariant
	vectorVariant
	unsupportedVariant
)

func (v outerVariant) String() string {
	switch v {
	case basicVariant:
		return "Basic"
	case vectorVariant:
		return "Vector"
	case unsupportedVariant:
		return "UNSUPPORTED"
	default:
		return fmt.Sprintf("invalid outerVariant %d", v)
	}
}

// panics on any values which aren't supported by dynfidl
// should be guarded by a call to `isSupported`
func visit(value gidlir.Value, decl gidlmixer.Declaration) visitResult {
	switch value := value.(type) {
	case bool:
		return visitResult{
			ValueStr:            strconv.FormatBool(value),
			OuterVariant:        basicVariant,
			InnerEnumAndVariant: "BasicField::Bool",
		}
	case int64, uint64, float64:
		suffix, basicOuterVariant := primitiveTypeName(decl.(gidlmixer.PrimitiveDeclaration).Subtype())
		return visitResult{
			ValueStr:            fmt.Sprintf("%v%s", value, suffix),
			OuterVariant:        basicVariant,
			InnerEnumAndVariant: fmt.Sprintf("BasicField::%s", basicOuterVariant),
		}
	case string:
		var valueStr string
		if fidlgen.PrintableASCII(value) {
			valueStr = fmt.Sprintf("String::from(%q).into_bytes()", value)
		} else {
			valueStr = fmt.Sprintf("b\"%s\".to_vec()", gidllibrust.EscapeStr(value))
		}
		return visitResult{
			ValueStr:            valueStr,
			OuterVariant:        vectorVariant,
			InnerEnumAndVariant: "VectorField::UInt8Vector",
		}
	case gidlir.Record:
		decl := decl.(*gidlmixer.StructDecl)
		valueStr := "Structure::default()"
		for _, field := range value.Fields {
			fieldDecl, _ := decl.Field(field.Key.Name)
			fieldResult := visit(field.Value, fieldDecl)
			valueStr += fmt.Sprintf(
				".field(Field::%s(%s(%s)))",
				fieldResult.OuterVariant,
				fieldResult.InnerEnumAndVariant,
				fieldResult.ValueStr)
		}
		return visitResult{
			ValueStr:            valueStr,
			OuterVariant:        unsupportedVariant,
			InnerEnumAndVariant: "UNUSED",
		}
	case []gidlir.Value:
		elemDecl := decl.(*gidlmixer.VectorDecl).Elem()
		var elements []string
		for _, item := range value {
			visited := visit(item, elemDecl)
			elements = append(elements, visited.ValueStr)
		}
		valueStr := fmt.Sprintf("vec![%s]", strings.Join(elements, ", "))

		var innerEnumAndVariant string
		switch decl := elemDecl.(type) {
		case gidlmixer.PrimitiveDeclaration:
			_, basicOuterVariant := primitiveTypeName(decl.Subtype())
			innerEnumAndVariant = fmt.Sprintf("VectorField::%sVector", basicOuterVariant)
		case *gidlmixer.StringDecl, *gidlmixer.VectorDecl:
			// vectors are only supported as vector elements if they're vector<uint8>
			// let rustc error if we're wrong about that assumption
			innerEnumAndVariant = "VectorField::UInt8VectorVector"
		default:
			panic(fmt.Sprintf("unexpected type for a vector element %s", decl))
		}

		return visitResult{
			ValueStr:            valueStr,
			OuterVariant:        vectorVariant,
			InnerEnumAndVariant: innerEnumAndVariant,
		}
	default:
		panic(fmt.Sprintf("unsupported type: %T", value))
	}
}

// panics on any values which aren't supported by dynfidl
// should be guarded by a call to `isSupported`
func primitiveTypeName(subtype fidlgen.PrimitiveSubtype) (string, string) {
	switch subtype {
	case fidlgen.Bool:
		return "bool", "Bool"
	case fidlgen.Int8:
		return "i8", "Int8"
	case fidlgen.Uint8:
		return "u8", "UInt8"
	case fidlgen.Int16:
		return "i16", "Int16"
	case fidlgen.Uint16:
		return "u16", "UInt16"
	case fidlgen.Int32:
		return "i32", "Int32"
	case fidlgen.Uint32:
		return "u32", "UInt32"
	case fidlgen.Int64:
		return "i64", "Int64"
	case fidlgen.Uint64:
		return "u64", "UInt64"
	default:
		panic(fmt.Sprintf("unsupported subtype %v", subtype))
	}
}
