// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"fmt"
	"io"
	"strconv"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var tmpl = template.Must(template.New("tmpls").Parse(`
package fidl_test

import (
	"testing"
	"reflect"

	"syscall/zx/fidl/conformance"
	"syscall/zx/fidl"
)

func TestAllSuccessCases(t *testing.T) {
{{ range .SuccessCases }}
{
{{ .ValueBuild }}
successCase{
	name: {{ .Name }},
	input: &{{ .Value }},
	bytes: {{ .Bytes }},
}.check(t)
}
{{ end }}
}

func TestAllEncodingFailureCases(t *testing.T) {
{{ range .EncodeFailureCases }}
{
{{ .ValueBuild }}
encodeFailureCase{
	name: {{ .Name }},
	input: &{{ .Value }},
	code: {{ .ErrorCode }},
}.check(t)
}
{{ end }}
}

func TestAllDecodingFailureCases(t *testing.T) {
{{ range .DecodeFailureCases }}
{
decodeFailureCase{
	name: {{ .Name }},
	valTyp: reflect.TypeOf((*{{ .ValueType }})(nil)),
	bytes: {{ .Bytes }},
	code: {{ .ErrorCode }},
}.check(t)
}
{{ end }}
}
`))

type tmplInput struct {
	SuccessCases       []successCase
	EncodeFailureCases []encodeFailureCase
	DecodeFailureCases []decodeFailureCase
}

type successCase struct {
	Name, ValueBuild, Value, Bytes string
}

type encodeFailureCase struct {
	Name, ValueBuild, Value, ErrorCode string
}

type decodeFailureCase struct {
	Name, ValueType, Bytes, ErrorCode string
}

// Generate generates Go tests.
func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	successCases, err := successCases(gidl.Success, fidl)
	if err != nil {
		return err
	}
	encodeFailureCases, err := encodeFailureCases(gidl.FailsToEncode, fidl)
	if err != nil {
		return err
	}
	decodeFailureCases, err := decodeFailureCases(gidl.FailsToDecode, fidl)
	if err != nil {
		return err
	}
	return tmpl.Execute(wr, tmplInput{
		SuccessCases:       successCases,
		EncodeFailureCases: encodeFailureCases,
		DecodeFailureCases: decodeFailureCases,
	})
}

func successCases(gidlSuccesses []gidlir.Success, fidl fidlir.Root) ([]successCase, error) {
	var successCases []successCase
	for _, success := range gidlSuccesses {
		decl, err := gidlmixer.ExtractDeclaration(success.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("success %s: %s", success.Name, err)
		}

		var valueBuilder goValueBuilder
		gidlmixer.Visit(&valueBuilder, success.Value, decl)

		successCases = append(successCases, successCase{
			Name:       strconv.Quote(success.Name),
			ValueBuild: valueBuilder.String(),
			Value:      valueBuilder.lastVar,
			Bytes:      bytesBuilder(success.Bytes),
		})
	}
	return successCases, nil
}

func encodeFailureCases(gidlEncodeFailures []gidlir.FailsToEncode, fidl fidlir.Root) ([]encodeFailureCase, error) {
	var encodeFailureCases []encodeFailureCase
	for _, encodeFailure := range gidlEncodeFailures {
		decl, err := gidlmixer.ExtractDeclarationUnsafe(encodeFailure.Value, fidl)
		if err != nil {
			return nil, fmt.Errorf("encodeFailure %s: %s", encodeFailure.Name, err)
		}

		var valueBuilder goValueBuilder
		gidlmixer.Visit(&valueBuilder, encodeFailure.Value, decl)

		code, err := goErrorCode(encodeFailure.Err)
		if err != nil {
			return nil, err
		}

		encodeFailureCases = append(encodeFailureCases, encodeFailureCase{
			Name:       strconv.Quote(encodeFailure.Name),
			ValueBuild: valueBuilder.String(),
			Value:      valueBuilder.lastVar,
			ErrorCode:  code,
		})
	}
	return encodeFailureCases, nil
}

func decodeFailureCases(gidlDecodeFailures []gidlir.FailsToDecode, fidl fidlir.Root) ([]decodeFailureCase, error) {
	var decodeFailureCases []decodeFailureCase
	for _, decodeFailure := range gidlDecodeFailures {
		code, err := goErrorCode(decodeFailure.Err)
		if err != nil {
			return nil, err
		}

		decodeFailureCases = append(decodeFailureCases, decodeFailureCase{
			Name:      strconv.Quote(decodeFailure.Name),
			ValueType: goType(decodeFailure.Type),
			Bytes:     bytesBuilder(decodeFailure.Bytes),
			ErrorCode: code,
		})
	}
	return decodeFailureCases, nil
}

func goType(irType string) string {
	return "conformance." + irType
}

func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	builder.WriteString("[]byte{\n")
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x", b))
		builder.WriteString(",")
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	builder.WriteString("}")
	return builder.String()
}

type goValueBuilder struct {
	strings.Builder
	varidx int

	lastVar string
}

func (b *goValueBuilder) newVar() string {
	b.varidx++
	return fmt.Sprintf("v%d", b.varidx)
}

func (b *goValueBuilder) OnBool(value bool) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"%s := %t\n", newVar, value))
	b.lastVar = newVar
}

func (b *goValueBuilder) OnInt64(value int64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"var %s %s = %d\n", newVar, typ, value))
	b.lastVar = newVar
}

func (b *goValueBuilder) OnUint64(value uint64, typ fidlir.PrimitiveSubtype) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"var %s %s = %d\n", newVar, typ, value))
	b.lastVar = newVar
}

func (b *goValueBuilder) OnString(value string) {
	newVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"%s := %s\n", newVar, strconv.Quote(value)))
	b.lastVar = newVar
}

func (b *goValueBuilder) OnStruct(value gidlir.Object, decl *gidlmixer.StructDecl) {
	b.onObject(value, decl)
}

func (b *goValueBuilder) onObject(value gidlir.Object, decl gidlmixer.KeyedDeclaration) {
	containerVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf(
		"var %s conformance.%s\n", containerVar, value.Name))
	for _, field := range value.Fields {
		fieldDecl, _ := decl.ForKey(field.Name)
		gidlmixer.Visit(b, field.Value, fieldDecl)
		fieldVar := b.lastVar

		switch structDecl := decl.(type) {
		case *gidlmixer.StructDecl:
			if structDecl.IsKeyNullable(field.Name) {
				fieldVar = "&" + fieldVar
			}
			b.Builder.WriteString(fmt.Sprintf(
				"%s.%s = %s\n", containerVar, fidlcommon.ToUpperCamelCase(field.Name), fieldVar))
		default:
			b.Builder.WriteString(fmt.Sprintf(
				"%s.Set%s(%s)\n", containerVar, fidlcommon.ToUpperCamelCase(field.Name), fieldVar))
		}
	}
	b.lastVar = containerVar
}

func (b *goValueBuilder) OnTable(value gidlir.Object, decl *gidlmixer.TableDecl) {
	b.onObject(value, decl)
}

func (b *goValueBuilder) OnXUnion(value gidlir.Object, decl *gidlmixer.XUnionDecl) {
	b.onObject(value, decl)
}

func (b *goValueBuilder) OnUnion(value gidlir.Object, decl *gidlmixer.UnionDecl) {
	b.onObject(value, decl)
}

func (b *goValueBuilder) onList(value []interface{}, decl gidlmixer.ListDeclaration) {
	var argStr string
	elemDecl, _ := decl.Elem()
	for _, item := range value {
		gidlmixer.Visit(b, item, elemDecl)
		argStr += b.lastVar + ", "
	}
	sliceVar := b.newVar()
	b.Builder.WriteString(fmt.Sprintf("%s := %s{%s}\n", sliceVar, typeName(decl), argStr))
	b.lastVar = sliceVar
}

func (b *goValueBuilder) OnArray(value []interface{}, decl *gidlmixer.ArrayDecl) {
	b.onList(value, decl)
}

func (b *goValueBuilder) OnVector(value []interface{}, decl *gidlmixer.VectorDecl) {
	b.onList(value, decl)
}

func typeName(decl gidlmixer.Declaration) string {
	switch decl := decl.(type) {
	case *gidlmixer.BoolDecl:
		return "bool"
	case *gidlmixer.NumberDecl:
		return string(decl.Typ)
	case *gidlmixer.StringDecl:
		return "string"
	case *gidlmixer.StructDecl:
		return identifierName(decl.Name)
	case *gidlmixer.TableDecl:
		return identifierName(decl.Name)
	case *gidlmixer.UnionDecl:
		return identifierName(decl.Name)
	case *gidlmixer.XUnionDecl:
		return identifierName(decl.Name)
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("[%d]%s", decl.Size(), elemName(decl))
	case *gidlmixer.VectorDecl:
		return fmt.Sprintf("[]%s", elemName(decl))
	default:
		panic("unhandled case")
	}
}

func identifierName(eci fidlir.EncodedCompoundIdentifier) string {
	parts := strings.Split(string(eci), "/")
	return strings.Join(parts, ".")
}

func elemName(parent gidlmixer.ListDeclaration) string {
	if elemDecl, ok := parent.Elem(); ok {
		return typeName(elemDecl)
	}
	panic("missing element")
}

var goErrorCodeNames = map[gidlir.ErrorCode]string{
	gidlir.StringTooLong:               "ErrStringTooLong",
	gidlir.NullEmptyStringWithNullBody: "ErrUnexpectedNullRef",
}

func goErrorCode(code gidlir.ErrorCode) (string, error) {
	if str, ok := goErrorCodeNames[code]; ok {
		return fmt.Sprintf("fidl.%s", str), nil
	}
	return "", fmt.Errorf("no go error string defined for error code %s", code)
}
