// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const XUnion = `
{{- define "XUnionDefinition" -}}
type {{ .TagName }} uint64
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
	{{ .TagName }} ` + "`" + `fidl:"x{{ if .IsStrict }}!{{end}},{{ .InlineSizeOld }},{{ .AlignmentOld }}" fidl_size_v1:"{{.InlineSizeV1}}" fidl_alignment_v1:"{{.AlignmentV1}}" fidl_size_v1_no_ee:"{{.InlineSizeV1}}" fidl_alignment_v1_no_ee:"{{.AlignmentV1}}"` + "`" + `
	{{- if .IsFlexible }}
	I_unknownData []byte
	{{- end }}
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type }}  ` + "`" + `fidl:"{{ .FidlTag }}"` + "`" + `
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

func (_m *{{ .Name }}) Ordinal() uint64 {
	return uint64(_m.{{ .TagName }})
}

{{- range .Members }}

func (_m *{{ $.Name }}) Set{{ .Name }}({{ .PrivateName }} {{ .Type }}) {
	_m.{{ $.TagName }} = {{ $.Name }}{{ .Name }}
	_m.{{ .Name }} = {{ .PrivateName }}
}

func {{ $.Name }}With{{ .Name }}({{ .PrivateName }} {{ .Type }}) {{ $.Name }} {
	var _u {{ $.Name }}
	_u.Set{{ .Name }}({{ .PrivateName }})
	return _u
}
{{- end }}

{{/* Note that there is no SetUnknownData() function. If you really need to set the I_unknownData
	 field (e.g. for testing), use Go's reflect package. */}}

{{- end -}}
`
