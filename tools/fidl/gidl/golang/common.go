// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"bytes"
	"fmt"
	"go/format"
	"io"
	"strconv"
	"strings"
	"text/template"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

// withGoFmt wraps a template that produces Go source code, and formats the
// execution result using go/format.
type withGoFmt struct {
	template *template.Template
}

func (w withGoFmt) Execute(wr io.Writer, data interface{}) error {
	var b bytes.Buffer
	if err := w.template.Execute(&b, data); err != nil {
		return err
	}
	formatted, err := format.Source(b.Bytes())
	if err != nil {
		return err
	}
	_, err = wr.Write(formatted)
	return err
}

func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	builder.WriteString("[]byte{\n")
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x,", b))
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	builder.WriteString("}")
	return builder.String()
}

func visit(value interface{}, decl gidlmixer.Declaration) string {
	switch value := value.(type) {
	case bool, int64, uint64, float64:
		switch decl := decl.(type) {
		case gidlmixer.PrimitiveDeclaration:
			return fmt.Sprintf("%#v", value)
		case *gidlmixer.BitsDecl, *gidlmixer.EnumDecl:
			return fmt.Sprintf("%s(%d)", typeLiteral(decl), value)
		}
	case string:
		if decl.IsNullable() {
			// Taking an address of a string literal is not allowed, so instead
			// we create a slice and get the address of its first element.
			return fmt.Sprintf("&[]string{%q}[0]", value)
		}
		return strconv.Quote(value)
	case gidlir.Record:
		if decl, ok := decl.(gidlmixer.RecordDeclaration); ok {
			return onRecord(value, decl)
		}
	case []interface{}:
		if decl, ok := decl.(gidlmixer.ListDeclaration); ok {
			return onList(value, decl)
		}
	case nil:
		if !decl.IsNullable() {
			panic(fmt.Sprintf("got nil for non-nullable type: %T", decl))
		}
		return "nil"
	}
	panic(fmt.Sprintf("not implemented: %T", value))
}

func onRecord(value gidlir.Record, decl gidlmixer.RecordDeclaration) string {
	var fields []string
	if decl, ok := decl.(*gidlmixer.UnionDecl); ok && len(value.Fields) >= 1 {
		field := value.Fields[0]
		fullName := declName(decl)
		var tagValue string
		if field.Key.IsUnknown() {
			tagValue = fmt.Sprintf("%d", field.Key.UnknownOrdinal)
		} else {
			fieldName := fidlcommon.ToUpperCamelCase(field.Key.Name)
			tagValue = fmt.Sprintf("%s%s", fullName, fieldName)
		}
		parts := strings.Split(string(decl.Name()), "/")
		unqualifiedName := fidlcommon.ToLowerCamelCase(parts[len(parts)-1])
		fields = append(fields,
			fmt.Sprintf("I_%sTag: %s", unqualifiedName, tagValue))
	}
	_, isTable := decl.(*gidlmixer.TableDecl)
	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			if isTable {
				panic("Go does not store unknown data for tables")
			}
			unknownData := field.Value.(gidlir.UnknownData)
			fields = append(fields,
				fmt.Sprintf("I_unknownData: %s", bytesBuilder(unknownData.Bytes)))
			continue
		}
		fieldName := fidlcommon.ToUpperCamelCase(field.Key.Name)
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fields = append(fields,
			fmt.Sprintf("%s: %s", fieldName, visit(field.Value, fieldDecl)))
		if isTable && field.Value != nil {
			fields = append(fields, fmt.Sprintf("%sPresent: true", fieldName))
		}
	}
	if len(fields) == 0 {
		return fmt.Sprintf("%s{}", typeLiteral(decl))
	}
	// Insert newlines so that gofmt can produce good results.
	return fmt.Sprintf("%s{\n%s,\n}", typeLiteral(decl), strings.Join(fields, ",\n"))
}

func onList(value []interface{}, decl gidlmixer.ListDeclaration) string {
	elemDecl := decl.Elem()
	var elements []string
	for _, item := range value {
		elements = append(elements, visit(item, elemDecl))
	}
	if len(elements) == 0 {
		return fmt.Sprintf("%s{}", typeLiteral(decl))
	}
	// Insert newlines so that gofmt can produce good results.
	return fmt.Sprintf("%s{\n%s,\n}", typeLiteral(decl), strings.Join(elements, ",\n"))
}

func typeName(decl gidlmixer.Declaration) string {
	return typeNameHelper(decl, "*")
}

func typeLiteral(decl gidlmixer.Declaration) string {
	return typeNameHelper(decl, "&")
}

func typeNameHelper(decl gidlmixer.Declaration, pointerPrefix string) string {
	if !decl.IsNullable() {
		pointerPrefix = ""
	}

	switch decl := decl.(type) {
	case gidlmixer.PrimitiveDeclaration:
		return string(decl.Subtype())
	case gidlmixer.NamedDeclaration:
		return pointerPrefix + declName(decl)
	case *gidlmixer.StringDecl:
		return pointerPrefix + "string"
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("[%d]%s", decl.Size(), typeName(decl.Elem()))
	case *gidlmixer.VectorDecl:
		return fmt.Sprintf("%s[]%s", pointerPrefix, typeName(decl.Elem()))
	default:
		panic("unhandled case")
	}
}

func declName(decl gidlmixer.NamedDeclaration) string {
	return identifierName(decl.Name())
}

// TODO(fxb/39407): Such utilities (and their accompanying tests) would be
// useful as part of fidlcommon or fidlir to do FIDL-to-<target_lang>
// conversion.
func identifierName(qualifiedName string) string {
	parts := strings.Split(qualifiedName, "/")
	lastPartsIndex := len(parts) - 1
	for i, part := range parts {
		if i == lastPartsIndex {
			parts[i] = fidlcommon.ToUpperCamelCase(part)
		} else {
			parts[i] = fidlcommon.ToSnakeCase(part)
		}
	}
	return strings.Join(parts, ".")
}

// Go errors are defined in third_party/go/src/syscall/zx/fidl/errors.go
var goErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.StringTooLong:              "ErrStringTooLong",
	gidlir.StringNotUtf8:              "ErrStringNotUTF8",
	gidlir.NonEmptyStringWithNullBody: "ErrUnexpectedNullRef",
	gidlir.StrictUnionFieldNotSet:     "ErrInvalidXUnionTag",
	gidlir.StrictUnionUnknownField:    "ErrInvalidXUnionTag",
	gidlir.InvalidPaddingByte:         "ErrNonZeroPadding",
	gidlir.StrictEnumUnknownValue:     "ErrInvalidEnumValue",
}

func goErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := goErrorCodeNames[code]; ok {
		return fmt.Sprintf("fidl.%s", str), nil
	}
	return "", fmt.Errorf("no go error string defined for error code %s", code)
}
