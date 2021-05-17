// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"fmt"
	"strings"

	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func BuildEqualityCheck(actualExpr string, expectedValue interface{}, decl gidlmixer.Declaration, koidArrayVar string) string {
	builder := equalityCheckBuilder{
		koidArrayVar: koidArrayVar,
	}
	builder.visit(actualExpr, expectedValue, decl)
	return builder.String()
}

// Generator of new variable names from a sequence.
type varSeq int

func (v *varSeq) next() string {
	*v++
	return fmt.Sprintf("f%d", *v)
}

type equalityCheckBuilder struct {
	strings.Builder
	varSeq       varSeq
	koidArrayVar string
}

func (b *equalityCheckBuilder) write(format string, vals ...interface{}) {
	b.WriteString(fmt.Sprintf(format, vals...))
}

func (b *equalityCheckBuilder) createAndAssignVar(val string) string {
	varName := b.varSeq.next()
	b.write("%s := %s\n", varName, val)
	b.write("ignore_unused_warning(%s)\n", varName)
	return varName
}

func (b *equalityCheckBuilder) assertEquals(actual, expected string) {
	b.write(
		`if %[1]s != %[2]s {
	t.Fatalf("unexpectedly unequal: %%s and %%s", %[1]q, %[2]q)
}
`, actual, expected)
}

func (b *equalityCheckBuilder) expectEquals(actual, expected string) {
	b.write(
		`if %[1]s != %[2]s {
	t.Errorf("unexpectedly unequal: %%s and %%s", %[1]q, %[2]q)
}
`, actual, expected)
}

func (b *equalityCheckBuilder) expectNotEquals(actual, expected string) {
	b.write(
		`if %[1]s == %[2]s {
	t.Errorf("unexpectedly equal: %%s and %%s", %[1]q, %[2]q)
}
`, actual, expected)
}

func (b *equalityCheckBuilder) expectFalse(value string) {
	b.write(
		`if %[1]s {
	t.Errorf("expected false but was true: %%s", %[1]q)
}
`, value)
}

func (b *equalityCheckBuilder) expectTrue(value string) {
	b.write(
		`if !(%[1]s) {
	t.Errorf("expected true but was false: %%s", %[1]q)
}
`, value)
}

func (b *equalityCheckBuilder) assertTrue(value string) {
	b.write(
		`if !(%[1]s) {
	t.Fatalf("expected true but was false: %%s", %[1]q)
}
`, value)
}

func (b *equalityCheckBuilder) expectNil(value string) {
	b.write(
		`if %[1]s != nil {
	t.Errorf("expected nil but was non-nil: %%s", %[1]q)
}
`, value)
}

func (b *equalityCheckBuilder) expectKoidEquals(actual string, expectedHandle gidlir.Handle) {
	var handleVar = fmt.Sprintf("&%s.Handle", b.createAndAssignVar(actual))
	infoVar := b.varSeq.next()
	b.write(
		`%s, err := handleGetBasicInfo(%s)
if err != nil {
	t.Fatal(err)
}
`, infoVar, handleVar)
	b.expectEquals(fmt.Sprintf("%s.Koid", infoVar), fmt.Sprintf("%s[%d]", b.koidArrayVar, expectedHandle))
}

func (b *equalityCheckBuilder) visit(actualExpr string, expectedValue interface{}, decl gidlmixer.Declaration) {
	switch expectedValue := expectedValue.(type) {
	case bool, int64, uint64, float64:
		b.expectEquals(actualExpr, fmt.Sprintf("%v", expectedValue))
		return
	case gidlir.RawFloat:
		switch decl.(*gidlmixer.FloatDecl).Subtype() {
		case fidlgen.Float32:
			b.expectEquals(fmt.Sprintf("math.Float32bits(%s)", actualExpr), fmt.Sprintf("%d", expectedValue))
			return
		case fidlgen.Float64:
			b.expectEquals(fmt.Sprintf("math.Float64bits(%s)", actualExpr), fmt.Sprintf("%d", expectedValue))
			return
		}
	case string:
		if decl.IsNullable() {
			actualExpr = b.createAndAssignVar(fmt.Sprintf("*(%s)", actualExpr))
		}
		b.expectEquals(actualExpr, fmt.Sprintf("%q", expectedValue))
		return
	case gidlir.HandleWithRights:
		b.visitHandle(actualExpr, expectedValue, decl.(*gidlmixer.HandleDecl))
		return
	case gidlir.Record:
		switch decl := decl.(type) {
		case *gidlmixer.StructDecl:
			b.visitStruct(actualExpr, expectedValue, decl)
			return
		case *gidlmixer.TableDecl:
			b.visitTable(actualExpr, expectedValue, decl)
			return
		case *gidlmixer.UnionDecl:
			b.visitUnion(actualExpr, expectedValue, decl)
			return
		}
	case []interface{}:
		b.visitList(actualExpr, expectedValue, decl.(gidlmixer.ListDeclaration))
		return
	case nil:
		switch decl.(type) {
		case *gidlmixer.HandleDecl:
			b.expectEquals(actualExpr, "zx.HandleInvalid")
			return
		default:
			b.expectNil(actualExpr)
			return
		}
	}
	panic(fmt.Sprintf("not implemented: %T (decl: %T)", expectedValue, decl))
}

func (b *equalityCheckBuilder) visitHandle(actualExpr string, expectedValue gidlir.HandleWithRights, decl *gidlmixer.HandleDecl) {
	var handleVar string
	if decl.Subtype() == fidlgen.Handle {
		handleVar = fmt.Sprintf("&%s", b.createAndAssignVar(actualExpr))
	} else {
		handleVar = fmt.Sprintf("%s.Handle()", actualExpr)
	}
	infoVar := b.varSeq.next()
	b.write(
		`%s, err := handleGetBasicInfo(%s)
if err != nil {
	t.Fatal(err)
}
`, infoVar, handleVar)
	b.expectEquals(fmt.Sprintf("%s.Koid", infoVar), fmt.Sprintf("%s[%d]", b.koidArrayVar, expectedValue.Handle))
	b.expectTrue(fmt.Sprintf("%[1]s.Type == %[2]d || %[2]d == zx.ObjectTypeNone", infoVar, expectedValue.Type))
	b.expectTrue(fmt.Sprintf("%[1]s.Rights == %[2]d || %[2]d == zx.RightSameRights", infoVar, expectedValue.Rights))
}

func (b *equalityCheckBuilder) visitStruct(actualExpr string, expectedValue gidlir.Record, decl *gidlmixer.StructDecl) {
	if decl.IsNullable() {
		actualExpr = b.createAndAssignVar(fmt.Sprintf("*(%s)", actualExpr))
	}
	actualVar := b.createAndAssignVar(actualExpr)
	for _, field := range expectedValue.Fields {
		fieldDecl, ok := decl.Field(field.Key.Name)
		if !ok {
			panic(fmt.Sprintf("field %q not found", field.Key.Name))
		}
		actualFieldExpr := fmt.Sprintf("%s.%s", actualVar, fidlgen.ToUpperCamelCase(field.Key.Name))
		b.visit(actualFieldExpr, field.Value, fieldDecl)
	}
}

func (b *equalityCheckBuilder) visitTable(actualExpr string, expectedValue gidlir.Record, decl *gidlmixer.TableDecl) {
	actualVar := b.createAndAssignVar(actualExpr)
	expectedFieldValues := map[string]gidlir.Value{}
	for _, field := range expectedValue.Fields {
		if field.Key.IsKnown() {
			expectedFieldValues[field.Key.Name] = field.Value
			continue
		}
		ud := field.Value.(gidlir.UnknownData)
		b.write(
			`if _, ok := %[1]s.GetUnknownData()[%[2]d]; !ok {
t.Fatalf("expected unknown data for %[1]s at ordinal: %[2]d")
}
`, actualVar, field.Key.UnknownOrdinal)
		b.assertEquals(fmt.Sprintf("len(%s.GetUnknownData()[%d].Bytes)", actualVar, field.Key.UnknownOrdinal), fmt.Sprintf("%d", len(ud.Bytes)))
		for i, byt := range ud.Bytes {
			b.expectEquals(fmt.Sprintf("%s.GetUnknownData()[%d].Bytes[%d]", actualVar, field.Key.UnknownOrdinal, i), fmt.Sprintf("%d", byt))
		}
		b.assertEquals(fmt.Sprintf("len(%s.GetUnknownData()[%d].Handles)", actualVar, field.Key.UnknownOrdinal), fmt.Sprintf("%d", len(ud.Handles)))
		for i, h := range ud.Handles {
			b.expectKoidEquals(fmt.Sprintf("%s.GetUnknownData()[%d].Handles[%d]", actualVar, field.Key.UnknownOrdinal, i), h)
		}
	}

	for _, fieldName := range decl.FieldNames() {
		fieldDecl, ok := decl.Field(fieldName)
		if !ok {
			panic(fmt.Sprintf("field decl %s not found", fieldName))
		}
		goFieldName := fidlgen.ToUpperCamelCase(fieldName)
		if expectedFieldValue, ok := expectedFieldValues[goFieldName]; ok {
			b.assertTrue(fmt.Sprintf("%s.Has%s()", actualVar, goFieldName))
			fieldVar := b.createAndAssignVar(fmt.Sprintf("%s.Get%s()", actualVar, goFieldName))
			b.visit(fieldVar, expectedFieldValue, fieldDecl)
		} else {
			b.expectFalse(fmt.Sprintf("%s.Has%s()", actualVar, goFieldName))
		}
	}
}

func (b *equalityCheckBuilder) visitUnion(actualExpr string, expectedValue gidlir.Record, decl *gidlmixer.UnionDecl) {
	if len(expectedValue.Fields) != 1 {
		panic("unions have exactly one assigned field")
	}
	actualVar := b.createAndAssignVar(actualExpr)
	field := expectedValue.Fields[0]
	if field.Key.IsUnknown() {
		ud := field.Value.(gidlir.UnknownData)
		b.assertEquals(fmt.Sprintf("len(%s.GetUnknownData().Bytes)", actualVar), fmt.Sprintf("%d", len(ud.Bytes)))
		for i, byt := range ud.Bytes {
			b.expectEquals(fmt.Sprintf("%s.GetUnknownData().Bytes[%d]", actualVar, i), fmt.Sprintf("%d", byt))
		}
		b.assertEquals(fmt.Sprintf("len(%s.GetUnknownData().Handles)", actualVar), fmt.Sprintf("%d", len(ud.Handles)))
		for i, h := range ud.Handles {
			b.expectKoidEquals(fmt.Sprintf("%s.GetUnknownData().Handles[%d]", actualVar, i), h)
		}
		return
	}

	fieldDecl, ok := decl.Field(field.Key.Name)
	if !ok {
		panic(fmt.Sprintf("field %q not found", field.Key.Name))
	}

	fieldName := fidlgen.ToUpperCamelCase(field.Key.Name)
	b.assertEquals(fmt.Sprintf("%s.Which()", actualVar),
		fmt.Sprintf("%s%s", declName(decl), fieldName))
	fieldVar := b.createAndAssignVar(fmt.Sprintf("%s.%s", actualVar, fieldName))
	b.visit(fieldVar, field.Value, fieldDecl)
}

func (b *equalityCheckBuilder) visitList(actualExpr string, expectedValue []interface{}, decl gidlmixer.ListDeclaration) {
	if decl.IsNullable() {
		actualExpr = fmt.Sprintf("*(%s)", actualExpr)
	}
	actualVar := b.createAndAssignVar(actualExpr)
	if _, ok := decl.(*gidlmixer.VectorDecl); ok {
		b.assertEquals(fmt.Sprintf("len(%s)", actualVar), fmt.Sprintf("%d", len(expectedValue)))
	}
	for i, item := range expectedValue {
		b.visit(fmt.Sprintf("%s[%d]", actualVar, i), item, decl.Elem())
	}
}
