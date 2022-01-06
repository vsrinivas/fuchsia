// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"fmt"
	"strconv"
	"strings"

	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidllibrust "go.fuchsia.dev/fuchsia/tools/fidl/gidl/librust"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func buildHandleDefs(defs []gidlir.HandleDef) string {
	if len(defs) == 0 {
		return ""
	}
	var builder strings.Builder
	builder.WriteString("[\n")
	for i, d := range defs {
		// Write indices corresponding to the .gidl file handle_defs block.
		builder.WriteString(fmt.Sprintf(
			`// #%d
HandleDef{
	subtype: HandleSubtype::%s,
	rights: Rights::from_bits(%d).unwrap(),
},
`, i, handleTypeName(d.Subtype), d.Rights))
	}
	builder.WriteString("]")
	return builder.String()
}

func buildHandles(handles []gidlir.Handle) string {
	var builder strings.Builder
	builder.WriteString("[\n")
	for i, h := range handles {
		builder.WriteString(fmt.Sprintf("%d,", h))
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	builder.WriteString("]")
	return builder.String()
}

func buildHandleValues(handles []gidlir.Handle) string {
	var builder strings.Builder
	builder.WriteString("vec![\n")
	for _, h := range handles {
		builder.WriteString(fmt.Sprintf("%s,", buildHandleValue(h)))
	}
	builder.WriteString("]")
	return builder.String()
}

func buildUnknownData(data gidlir.UnknownData, isResource bool) string {
	if !isResource {
		return fmt.Sprintf("vec!%s", gidllibrust.BuildBytes(data.Bytes))
	}
	return fmt.Sprintf(
		"UnknownData { bytes: vec!%s, handles: %s }",
		gidllibrust.BuildBytes(data.Bytes),
		buildHandleValues(data.Handles))
}

func visit(value gidlir.Value, decl gidlmixer.Declaration) string {
	switch value := value.(type) {
	case bool:
		return strconv.FormatBool(value)
	case int64, uint64, float64:
		switch decl := decl.(type) {
		case gidlmixer.PrimitiveDeclaration:
			suffix := primitiveTypeName(decl.Subtype())
			return fmt.Sprintf("%v%s", value, suffix)
		case *gidlmixer.BitsDecl:
			primitive := visit(value, &decl.Underlying)
			if decl.IsFlexible() {
				return fmt.Sprintf("%s::from_bits_allow_unknown(%v)", declName(decl), primitive)
			}
			// Use from_bits_unchecked so that encode_failure tests work. It's
			// not worth the effort to make the test type available here and use
			// from_bits(...).unwrap() in success cases, since all this would do
			// is move validation from the bindings to GIDL.
			return fmt.Sprintf("unsafe { %s::from_bits_unchecked(%v) }", declName(decl), primitive)
		case *gidlmixer.EnumDecl:
			primitive := visit(value, &decl.Underlying)
			if decl.IsFlexible() {
				return fmt.Sprintf("%s::from_primitive_allow_unknown(%v)", declName(decl), primitive)
			}
			return fmt.Sprintf("%s::from_primitive(%v).unwrap()", declName(decl), primitive)
		}
	case gidlir.RawFloat:
		switch decl.(*gidlmixer.FloatDecl).Subtype() {
		case fidlgen.Float32:
			return fmt.Sprintf("f32::from_bits(%#b)", value)
		case fidlgen.Float64:
			return fmt.Sprintf("f64::from_bits(%#b)", value)
		}
	case string:
		var expr string
		if fidlgen.PrintableASCII(value) {
			expr = fmt.Sprintf("String::from(%q)", value)
		} else {
			expr = fmt.Sprintf("std::str::from_utf8(b\"%s\").unwrap().to_string()", gidllibrust.EscapeStr(value))
		}
		return wrapNullable(decl, expr)
	case gidlir.HandleWithRights:
		expr := buildHandleValue(value.Handle)
		return wrapNullable(decl, expr)
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return onStruct(value, decl)
		case *gidlmixer.TableDecl:
			return onTable(value, decl)
		case *gidlmixer.UnionDecl:
			return onUnion(value, decl)
		}
	case []gidlir.Value:
		switch decl := decl.(type) {
		case *gidlmixer.ArrayDecl:
			return onList(value, decl)
		case *gidlmixer.VectorDecl:
			return onList(value, decl)
		}
	case nil:
		if !decl.IsNullable() {
			panic(fmt.Sprintf("got nil for non-nullable type: %T", decl))
		}
		return "None"
	}
	panic(fmt.Sprintf("not implemented: %T", value))
}

func declName(decl gidlmixer.NamedDeclaration) string {
	return identifierName(decl.Name())
}

// TODO(fxbug.dev/39407): Move into a common library outside GIDL.
func identifierName(qualifiedName string) string {
	parts := strings.Split(qualifiedName, "/")
	library_parts := strings.Split(parts[0], ".")
	return fmt.Sprintf("%s::%s", strings.Join(library_parts, "_"),
		fidlgen.ToUpperCamelCase(parts[1]))
}

func primitiveTypeName(subtype fidlgen.PrimitiveSubtype) string {
	switch subtype {
	case fidlgen.Bool:
		return "bool"
	case fidlgen.Int8:
		return "i8"
	case fidlgen.Uint8:
		return "u8"
	case fidlgen.Int16:
		return "i16"
	case fidlgen.Uint16:
		return "u16"
	case fidlgen.Int32:
		return "i32"
	case fidlgen.Uint32:
		return "u32"
	case fidlgen.Int64:
		return "i64"
	case fidlgen.Uint64:
		return "u64"
	case fidlgen.Float32:
		return "f32"
	case fidlgen.Float64:
		return "f64"
	default:
		panic(fmt.Sprintf("unexpected subtype %v", subtype))
	}
}

func handleTypeName(subtype fidlgen.HandleSubtype) string {
	switch subtype {
	case fidlgen.Handle:
		return "Handle"
	case fidlgen.Channel:
		return "Channel"
	case fidlgen.Event:
		return "Event"
	default:
		panic(fmt.Sprintf("unsupported handle subtype: %s", subtype))
	}
}

func wrapNullable(decl gidlmixer.Declaration, valueStr string) string {
	if !decl.IsNullable() {
		return valueStr
	}
	switch decl.(type) {
	case *gidlmixer.ArrayDecl, *gidlmixer.VectorDecl, *gidlmixer.StringDecl, *gidlmixer.HandleDecl:
		return fmt.Sprintf("Some(%s)", valueStr)
	case *gidlmixer.StructDecl, *gidlmixer.UnionDecl:
		return fmt.Sprintf("Some(Box::new(%s))", valueStr)
	case *gidlmixer.BoolDecl, *gidlmixer.IntegerDecl, *gidlmixer.FloatDecl, *gidlmixer.TableDecl:
		panic(fmt.Sprintf("decl %v should not be nullable", decl))
	}
	panic(fmt.Sprintf("unexpected decl %v", decl))
}

func onStruct(value gidlir.Record, decl *gidlmixer.StructDecl) string {
	var structFields []string
	providedKeys := make(map[string]struct{}, len(value.Fields))
	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		providedKeys[field.Key.Name] = struct{}{}
		fieldName := fidlgen.ToSnakeCase(field.Key.Name)
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldValueStr := visit(field.Value, fieldDecl)
		structFields = append(structFields, fmt.Sprintf("%s: %s", fieldName, fieldValueStr))
	}
	for _, key := range decl.FieldNames() {
		if _, ok := providedKeys[key]; !ok {
			fieldName := fidlgen.ToSnakeCase(key)
			structFields = append(structFields, fmt.Sprintf("%s: None", fieldName))
		}
	}
	valueStr := fmt.Sprintf("%s { %s }", declName(decl), strings.Join(structFields, ", "))
	return wrapNullable(decl, valueStr)
}

func onTable(value gidlir.Record, decl *gidlmixer.TableDecl) string {
	var tableFields []string
	var unknownTuples []string
	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			unknownTuples = append(unknownTuples, fmt.Sprintf("(%d, %s)",
				field.Key.UnknownOrdinal,
				buildUnknownData(field.Value.(gidlir.UnknownData), decl.IsResourceType())))
			continue
		}
		fieldName := fidlgen.ToSnakeCase(field.Key.Name)
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldValueStr := visit(field.Value, fieldDecl)
		tableFields = append(tableFields, fmt.Sprintf("%s: Some(%s)", fieldName, fieldValueStr))
	}
	unknownData := func() string {
		if len(unknownTuples) == 0 {
			return "None"
		}
		var b strings.Builder
		b.WriteString("Some([")
		for _, tuple := range unknownTuples {
			b.WriteString(tuple)
		}
		b.WriteString("].into())")
		return b.String()
	}
	tableFields = append(tableFields, fmt.Sprintf("unknown_data: %s", unknownData()))
	tableName := declName(decl)
	tableFields = append(tableFields, fmt.Sprintf("..%s::EMPTY", tableName))
	valueStr := fmt.Sprintf("%s { %s }", tableName, strings.Join(tableFields, ", "))
	return wrapNullable(decl, valueStr)
}

func onUnion(value gidlir.Record, decl *gidlmixer.UnionDecl) string {
	if len(value.Fields) != 1 {
		panic(fmt.Sprintf("union has %d fields, expected 1", len(value.Fields)))
	}
	field := value.Fields[0]
	var valueStr string
	if field.Key.IsUnknown() {
		unknownData := field.Value.(gidlir.UnknownData)
		valueStr = fmt.Sprintf(
			"%s::unknown(%d, %s)",
			declName(decl),
			field.Key.UnknownOrdinal,
			buildUnknownData(unknownData, decl.IsResourceType()),
		)
	} else {
		fieldName := fidlgen.ToUpperCamelCase(field.Key.Name)
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		fieldValueStr := visit(field.Value, fieldDecl)
		valueStr = fmt.Sprintf("%s::%s(%s)", declName(decl), fieldName, fieldValueStr)
	}
	return wrapNullable(decl, valueStr)
}

func onList(value []gidlir.Value, decl gidlmixer.ListDeclaration) string {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		elements = append(elements, visit(item, elemDecl))
	}
	elementsStr := strings.Join(elements, ", ")
	var valueStr string
	switch decl.(type) {
	case *gidlmixer.ArrayDecl:
		valueStr = fmt.Sprintf("[%s]", elementsStr)
	case *gidlmixer.VectorDecl:
		valueStr = fmt.Sprintf("vec![%s]", elementsStr)
	default:
		panic(fmt.Sprintf("unexpected decl %v", decl))
	}
	return wrapNullable(decl, valueStr)
}

func buildHandleValue(handle gidlir.Handle) string {
	return fmt.Sprintf("unsafe { copy_handle(&handle_defs[%d]) }", handle)
}
