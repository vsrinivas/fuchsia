// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"text/template"
)

const structTmplText = `
{{- define "Struct" -}}
{{$struct := . -}}
{{ template "StructDecl" $struct }}

{{ template "RuntimeTypeAccessors" $struct }}

{{ template "StructEncodingTmpl" $struct }}

{{ template "StructVersions" $struct }}

{{ template "StructDecodingTmpl" $struct }}

{{- range $enum := $struct.NestedEnums }}
{{ template "EnumDecl" $enum }}
{{- end}}
{{- end -}}
`

const structDeclTmplText = `
{{- define "StructDecl" -}}
{{$struct := . -}}
type {{$struct.Name}} struct {
{{- range $field := $struct.Fields}}
	{{$field.Name}} {{$field.Type}}
{{- end}}
}
{{- end -}}
`

const structEncodingTmplText = `
{{- define "StructEncodingTmpl" -}}
{{ $struct := . }}
func (s *{{$struct.Name}}) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct({{$struct.CurVersionSize}}, {{$struct.CurVersionNumber}})
	{{- range $field := $struct.Fields}}
		{{ template "FieldEncodingTmpl" $field.EncodingInfo }}
	{{- end}}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}
{{- end -}}
`

const structVersions = `
{{- define "StructVersions" -}}
{{- $struct := . -}}
var {{$struct.PrivateName}}_Versions []bindings.DataHeader = []bindings.DataHeader{
	{{- range $version := $struct.Versions}}
	bindings.DataHeader{ {{$version.NumBytes}}, {{$version.Version}} },
	{{- end}}
}
{{- end -}}
`

const structDecodingTmplText = `
{{- define "StructDecodingTmpl" -}}
{{- $struct := . -}}
func (s *{{$struct.Name}}) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len({{$struct.PrivateName}}_Versions), func(i int) bool {
		return {{$struct.PrivateName}}_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len({{$struct.PrivateName}}_Versions) {
		if {{$struct.PrivateName}}_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := {{$struct.PrivateName}}_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}

	{{- range $field := $struct.Fields}}
	if header.ElementsOrVersion >= {{$field.MinVersion}} {
		{{ template "FieldDecodingTmpl" $field.EncodingInfo }}
	}
	{{- end}}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}
{{- end -}}
`

func initStructTemplates() {
	template.Must(goFileTmpl.Parse(structEncodingTmplText))
	template.Must(goFileTmpl.Parse(structDeclTmplText))
	template.Must(goFileTmpl.Parse(structVersions))
	template.Must(goFileTmpl.Parse(structDecodingTmplText))
	template.Must(goFileTmpl.Parse(structTmplText))
}
