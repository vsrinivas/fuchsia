// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const XUnion = `
{{- define "XUnionDefinition" -}}
type {{ .TagName }} uint32
const (
	{{- if .IsFlexible }}
	{{ $.Name }}_unknownData = 0  // 0x00000000
	{{- end }}
	{{- range .Members }}
	{{ $.Name }}{{ .Name }} = {{ .Ordinal }} // {{ .Ordinal | printf "%#08x" }}
	{{- end }}
)

{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} struct {
	{{ .TagName }} ` + "`" + `fidl2:"x{{ if .IsStrict }}!{{end}},{{ .Size }},{{ .Alignment }}"` + "`" + `
	I_unknownData []byte
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type }} {{ .Tags }}
	{{- end }}
}

func (_m *{{ .Name }}) Which() {{ .TagName }} {
	{{- if .IsStrict }}
	return _m.{{ .TagName }}
	{{- else }}
	switch _m.{{ .TagName }} {
	{{- range .Members }}
	case {{ .Ordinal }}:
		return {{ $.Name }}{{ .Name }}
	{{- end }}
	default:
		return {{ $.Name }}_unknownData
	}
	{{- end }}
}

func (_m *{{ .Name }}) Ordinal() uint32 {
	return uint32(_m.{{ .TagName }})
}

{{- range .Members }}

func (_m *{{ $.Name }}) Set{{ .Name }}({{ .PrivateName }} {{ .Type }}) {
	_m.{{ $.TagName }} = {{ $.Name }}{{ .Name }}
	_m.{{ .Name }} = {{ .PrivateName }}
}
{{- end }}

{{/* Note that there is no SetUnknownData() function. If you really need to set the I_unknownData
	 field (e.g. for testing), use Go's reflect package. */}}

{{- end -}}
`
