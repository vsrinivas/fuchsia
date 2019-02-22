// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const XUnion = `
{{- define "XUnionDefinition" -}}
type {{ .TagName }} uint32
const (
	{{- range .Members }}
	{{ $.Name }}{{ .Name }} = {{ .Ordinal }} // {{ .Ordinal | printf "%#x" }}
	{{- end }}
)

{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} struct {
	{{ .TagName }} ` + "`" + `fidl2:"x,{{ .Size }},{{ .Alignment }}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type }} {{ .Tags }}
	{{- end }}
}

// Implements Payload.
func (_ *{{ .Name }}) InlineAlignment() int {
	return {{ .Alignment }}
}

// Implements Payload.
func (_ *{{ .Name }}) InlineSize() int {
	return {{ .Size }}
}

func (_m *{{ .Name }}) Which() {{ .TagName }} {
	return _m.{{ .TagName }}
}

{{- range .Members }}

func (_m *{{ $.Name }}) Set{{ .Name }}({{ .PrivateName }} {{ .Type }}) {
	_m.{{ $.TagName }} = {{ $.Name }}{{ .Name }}
	_m.{{ .Name }} = {{ .PrivateName }}
}
{{- end }}

{{- end -}}
`
