// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"text/template"
)

const ampersandIfNullable = `
{{- define "AmpersandIfNullable" -}}
{{- $info := . -}}
{{- if $info.IsNullable -}}
&
{{- end -}}
{{- end -}}
`

const starIfNullable = `
{{- define "StarIfNullable" -}}
{{- $info := . -}}
{{- if $info.IsNullable -}}
*
{{- end -}}
{{- end -}}
`

const fieldDecodingTmplText = `
{{- define "FieldDecodingTmpl" -}}
{{- $info := . -}}
{{- if $info.IsPointer -}}
pointer, err := decoder.ReadPointer()
if err != nil {
	return err
}
if pointer == 0 {
{{- if $info.IsNullable}}
	{{$info.Identifier}} = nil
{{- else if $info.IsUnion}}
	return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null union pointer"}
{{- else}}
	return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
{{- end }}
} else {
	{{ template "NonNullableFieldDecodingTmpl" $info }}
}
{{- else -}}
{{ template "NonNullableFieldDecodingTmpl" $info }}
{{- end -}}
{{- end -}}
`

const nonNullableFieldDecodingTmplText = `
{{- define "NonNullableFieldDecodingTmpl" -}}
{{- $info := . -}}
{{- if or $info.IsEnum $info.IsSimple -}}
value, err := decoder.{{$info.ReadFunction}}()
if err != nil {
	return err
}
{{if $info.IsSimple -}}
{{$info.Identifier}} = {{template "AmpersandIfNullable" $info}}value
{{- else -}}
{{$info.Identifier}} = {{$info.GoType}}(value)
{{- end -}}
{{- else if $info.IsHandle -}}
handle, err := decoder.{{$info.ReadFunction}}()
if err != nil {
	return err
}
if handle.IsValid() {
{{- if $info.IsNullable -}}
	{{$info.Identifier}} = &handle
} else {
	{{$info.Identifier}} = nil
{{- else -}}
	{{$info.Identifier}} = handle
} else {
	return &bindings.ValidationError{bindings.UnexpectedInvalidHandle, "unexpected invalid handle"}
{{- end -}}
}
{{- else if $info.IsStruct -}}
{{- if $info.IsNullable -}}
{{- if or $info.IsMap $info.IsArray -}}
{{$info.Identifier}} = {{$info.BaseGoType}}{}
{{- else -}}
{{$info.Identifier}} = new({{$info.BaseGoType}})
{{- end}}
{{end -}}
if err := {{$info.Identifier}}.Decode(decoder); err != nil {
	return err
}
{{- else if $info.IsUnion -}}
{{- if $info.IsPointer -}}
if err := decoder.StartNestedUnion(); err != nil {
	return err
}
{{end -}}
var err error
{{$info.Identifier}}, err = {{$info.UnionDecodeFunction}}(decoder)
if err != nil {
	return err
}
{{- if not $info.IsNullable}}
if {{$info.Identifier}} == nil {
	return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
}
{{- end -}}
{{- if $info.IsPointer}}
decoder.Finish()
{{- end -}}
{{- else if $info.IsInterface -}}
handle, err := decoder.{{$info.ReadFunction}}()
if err != nil {
	return err
}
if handle.IsValid() {
	handleOwner := bindings.NewMessagePipeHandleOwner(handle)
  {{$info.Identifier}} = {{template "AmpersandIfNullable" $info}}{{$info.BaseGoType}}{handleOwner}
} else {
{{- if $info.IsNullable}}
	{{$info.Identifier}} = nil
{{- else}}
	return &bindings.ValidationError{bindings.UnexpectedInvalidHandle, "unexpected invalid handle"}
{{- end -}}
}
{{- else if $info.IsArray -}}
{{- if $info.IsNullable -}}
{{$info.Identifier}} = new({{$info.BaseGoType}})
{{- end -}}
{{ $elInfo := $info.ElementEncodingInfo }}
len0, err := decoder.StartArray({{$elInfo.BitSize}})
if err != nil {
	return err
}
{{if $info.HasFixedSize -}}
if len0 != {{$info.FixedSize}} {
	return &bindings.ValidationError{bindings.UnexpectedArrayHeader,
		fmt.Sprintf("invalid array length: expected %d, got %d", {{$info.FixedSize}}, len0)}
}
{{else -}}
{{$info.DerefIdentifier}} = make({{$info.BaseGoType}}, len0)
{{- end}}
for i := uint32(0); i < len0; i++ {
  var {{$elInfo.Identifier}} {{$elInfo.GoType}}
	{{ template "FieldDecodingTmpl" $elInfo }}
	{{$info.DerefIdentifier}}[i] = {{$elInfo.Identifier}}
}
if err := decoder.Finish(); err != nil {
	return nil
}
{{- else if $info.IsMap -}}
{{ $keyInfo := $info.KeyEncodingInfo -}}
{{ $keyElId := $info.KeyEncodingInfo.ElementEncodingInfo.Identifier -}}
{{ $valueInfo := $info.ValueEncodingInfo -}}
{{ $valueElId := $info.ValueEncodingInfo.ElementEncodingInfo.Identifier -}}
{{- if or $info.IsMap $info.IsArray -}}
{{- if $info.IsNullable -}}
{{$info.Identifier}} = new({{$info.BaseGoType}})
{{- end}}
{{$info.DerefIdentifier}} = {{$info.BaseGoType}}{}
{{- else -}}
{{$info.Identifier}} = new({{$info.BaseGoType}})
{{- end}}
if err := decoder.StartMap(); err != nil {
	return err
}
var {{$keyInfo.Identifier}} {{$keyInfo.GoType}}
{
	{{ template "FieldDecodingTmpl" $keyInfo }}
}
var {{$valueInfo.Identifier}} {{$valueInfo.GoType}}
{
	{{ template "FieldDecodingTmpl" $valueInfo }}
}
if err := decoder.Finish(); err != nil {
	return nil
}
if len({{$keyInfo.Identifier}}) != len({{$valueInfo.Identifier}}) {
	return &bindings.ValidationError{bindings.DifferentSizedArraysInMap,
		fmt.Sprintf("Number of keys %d is different from number of values %d",
		len({{$keyInfo.Identifier}}), len({{$valueInfo.Identifier}}))}
}
for i := 0; i < len({{$keyInfo.Identifier}}); i++ {
	{{$info.DerefIdentifier}}[{{$keyInfo.Identifier}}[i]] = {{$valueInfo.Identifier}}[i]
}
{{- end -}}
{{- end -}}
`

func initDecodingTemplates() {
	template.Must(goFileTmpl.Parse(nonNullableFieldDecodingTmplText))
	template.Must(goFileTmpl.Parse(fieldDecodingTmplText))
	template.Must(goFileTmpl.Parse(ampersandIfNullable))
	template.Must(goFileTmpl.Parse(starIfNullable))
}
