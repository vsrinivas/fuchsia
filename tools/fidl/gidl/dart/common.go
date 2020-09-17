// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dart

import (
	"fmt"
	"log"
	"strconv"
	"strings"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

func buildHandleDefs(defs []gidlir.HandleDef) string {
	if len(defs) == 0 {
		return ""
	}
	var builder strings.Builder
	builder.WriteString("[\n")
	for i, d := range defs {
		switch d.Subtype {
		case fidlir.Channel:
			builder.WriteString(fmt.Sprint("HandleSubtype.event,"))
		case fidlir.Event:
			builder.WriteString(fmt.Sprint("HandleSubtype.channel,"))
		default:
			log.Fatal("unsupported handle subtype ", d.Subtype)
		}
		// Write indices corresponding to the .gidl file handle_defs block.
		builder.WriteString(fmt.Sprintf(" // #%d\n", i))
	}
	builder.WriteString("]")
	return builder.String()
}

func visit(value interface{}, decl gidlmixer.Declaration) string {
	switch value := value.(type) {
	case bool:
		return strconv.FormatBool(value)
	case int64, uint64, float64:
		switch decl := decl.(type) {
		case *gidlmixer.IntegerDecl, *gidlmixer.FloatDecl:
			return fmt.Sprintf("%#v", value)
		case gidlmixer.NamedDeclaration:
			return fmt.Sprintf("%s.ctor(%#v)", typeName(decl), value)
		}
	case string:
		return toDartStr(value)
	case gidlir.Handle:
		rawHandle := fmt.Sprintf("handleDefs[%d]", value)
		handleDecl := decl.(*gidlmixer.HandleDecl)
		switch handleDecl.Subtype() {
		case fidlir.Handle:
			return rawHandle
		case fidlir.Channel:
			return fmt.Sprintf("Channel(%s)", rawHandle)
		case fidlir.Event:
			// Dart does not support events, so events are mapped to bare handles
			return rawHandle
		default:
			log.Fatal("Handle subtype not supported ", handleDecl.Subtype())
		}
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			return onRecord(value, decl)
		case *gidlmixer.TableDecl:
			return onRecord(value, decl)
		case *gidlmixer.UnionDecl:
			return onUnion(value, decl)
		}
	case []interface{}:
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
		return "null"

	}
	panic(fmt.Sprintf("not implemented: %T", value))
}

func onRecord(value gidlir.Record, decl gidlmixer.RecordDeclaration) string {
	var args []string
	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			panic("unknown field not supported")
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		val := visit(field.Value, fieldDecl)
		args = append(args, fmt.Sprintf("%s: %s", fidlcommon.ToLowerCamelCase(field.Key.Name), val))
	}
	return fmt.Sprintf("%s(%s)", fidlcommon.ToUpperCamelCase(value.Name), strings.Join(args, ", "))
}

func onUnion(value gidlir.Record, decl *gidlmixer.UnionDecl) string {
	for _, field := range value.Fields {
		if field.Key.IsUnknown() {
			unknownData := field.Value.(gidlir.UnknownData)
			return fmt.Sprintf(
				"%s.with$UnknownData(%d, fidl.UnknownRawData(%s, []))",
				value.Name,
				field.Key.UnknownOrdinal,
				buildBytes(unknownData.Bytes))
		}
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %s not found", field.Key.Name))
		}
		val := visit(field.Value, fieldDecl)
		return fmt.Sprintf("%s.with%s(%s)", value.Name, fidlcommon.ToUpperCamelCase(field.Key.Name), val)
	}
	// Not currently possible to construct a union in dart with an invalid value.
	panic("unions must have a value set")
}

func onList(value []interface{}, decl gidlmixer.ListDeclaration) string {
	var elements []string
	elemDecl := decl.Elem()
	for _, item := range value {
		elements = append(elements, visit(item, elemDecl))
	}
	if integerDecl, ok := elemDecl.(*gidlmixer.IntegerDecl); ok {
		typeName := fidlcommon.ToUpperCamelCase(string(integerDecl.Subtype()))
		return fmt.Sprintf("%sList.fromList([%s])", typeName, strings.Join(elements, ", "))
	}
	if floatDecl, ok := elemDecl.(*gidlmixer.FloatDecl); ok {
		typeName := fidlcommon.ToUpperCamelCase(string(floatDecl.Subtype()))
		return fmt.Sprintf("%sList.fromList([%s])", typeName, strings.Join(elements, ", "))
	}
	return fmt.Sprintf("[%s]", strings.Join(elements, ", "))
}

func typeName(decl gidlmixer.NamedDeclaration) string {
	parts := strings.Split(decl.Name(), "/")
	lastPart := parts[len(parts)-1]
	return dartTypeName(lastPart)
}
