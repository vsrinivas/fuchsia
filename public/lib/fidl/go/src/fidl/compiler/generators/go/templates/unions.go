// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"text/template"
)

const unionTmplText = `
{{- define "Union" -}}
{{- $union := . }}
{{ template "UnionInterfaceDecl" $union }}

{{ template "StaticRuntimeTypeAccessor" $union }}

{{ template "UnknownUnionFieldDecl" $union }}
{{ template "UnknownUnionFieldEncode" $union }}

{{- range $field := $union.Fields}}
{{ template "UnionFieldDecl" $field }}

{{ template "UnionFieldEncode" $field }}

{{ template "UnionFieldDecode" $field }}

{{- if and GenTypeInfo $union.TypeKey -}}
{{ template "RuntimeTypeAccessor" $field }}
{{- end -}}
{{- end}}

{{ template "UnionDecode" $union }}

{{- end -}}
`

const unionInterfaceDeclTmplText = `
{{- define "UnionInterfaceDecl" -}}
{{$union := . -}}
type {{$union.Name}} interface {
	Tag() uint32
	Interface() interface{}
	__Reflect(__{{$union.Name}}Reflect)
	Encode(encoder *bindings.Encoder) error
}

type __{{$union.Name}}Reflect struct {
{{- range $field := $union.Fields}}
	{{$field.Name}} {{$field.Type}}
{{- end}}
}
{{- end -}}
`

const unionFieldDeclTmplText = `
{{- define "UnionFieldDecl" -}}
{{$field := . -}}
{{$unionName := $field.Union.Name -}}
type {{$unionName}}{{$field.Name}} struct{ Value {{$field.Type}} }
func (u *{{$unionName}}{{$field.Name}}) Tag() uint32 { return {{$field.Tag}} }
func (u *{{$unionName}}{{$field.Name}}) Interface() interface{} { return u.Value }
func (u *{{$unionName}}{{$field.Name}}) __Reflect(__{{$unionName}}Reflect) {}
{{- end -}}
`

const unionFieldEncodeTmplText = `
{{- define "UnionFieldEncode" -}}
{{$field := . -}}
{{$unionName := $field.Union.Name -}}
func (u *{{$unionName}}{{$field.Name}}) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	{{ template "FieldEncodingTmpl" $field.EncodingInfo }}
	
	encoder.FinishWritingUnionValue()
	return nil
}
{{- end -}}
`

const unknownUnionFieldDeclTmplText = `
{{- define "UnknownUnionFieldDecl" -}}
{{$union := . -}}
type {{$union.Name}}Unknown struct { tag uint32 }
func (u *{{$union.Name}}Unknown) Tag() uint32 { return u.tag }
func (u *{{$union.Name}}Unknown) Interface() interface{} { return nil }
func (u *{{$union.Name}}Unknown) __Reflect(__{{$union.Name}}Reflect) {}
{{- end -}}
`

const unknownUnionFieldEncodeTmplText = `
{{- define "UnknownUnionFieldEncode" -}}
{{$union := . -}}
func (u *{{$union.Name}}Unknown) Encode(encoder *bindings.Encoder) error {
	return fmt.Errorf("Trying to serialize an unknown {{$union.Name}}. There is no sane way to do that!");
}
{{- end -}}
`

const unionFieldDecodeTmplText = `
{{- define "UnionFieldDecode" -}}
{{$field := . -}}
{{$unionName := $field.Union.Name -}}
func (u *{{$unionName}}{{$field.Name}}) decodeInternal(decoder *bindings.Decoder) error {
	{{ template "FieldDecodingTmpl" $field.EncodingInfo }}

	return nil
}
{{- end -}}
`

const unionDecodeTmplText = `
{{- define "UnionDecode" -}}
{{$union := . -}}
func Decode{{$union.Name}}(decoder *bindings.Decoder) ({{$union.Name}}, error) {
	size, tag, err := decoder.ReadUnionHeader()
	if err != nil {
		return nil, err
	}

	if size == 0 {
		decoder.SkipUnionValue()
		return nil, nil
	}

	switch tag {
{{- range $field := $union.Fields}}
	case {{$field.Tag}}:
		var value {{$union.Name}}{{$field.Name}}
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
{{- end -}}
	}

	decoder.SkipUnionValue()
	return &{{$union.Name}}Unknown{tag: tag}, nil
}
{{- end -}}
`

func initUnionTemplates() {
	template.Must(goFileTmpl.Parse(unionTmplText))
	template.Must(goFileTmpl.Parse(unionInterfaceDeclTmplText))
	template.Must(goFileTmpl.Parse(unionFieldDeclTmplText))
	template.Must(goFileTmpl.Parse(unknownUnionFieldDeclTmplText))
	template.Must(goFileTmpl.Parse(unknownUnionFieldEncodeTmplText))
	template.Must(goFileTmpl.Parse(unionFieldEncodeTmplText))
	template.Must(goFileTmpl.Parse(unionFieldDecodeTmplText))
	template.Must(goFileTmpl.Parse(unionDecodeTmplText))
}
