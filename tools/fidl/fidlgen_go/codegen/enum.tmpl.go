// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const enumTmpl = `
{{- define "EnumDefinition" -}}
{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} {{ .Type }}
const (
	{{- range .Members }}
	{{ $.Name }}{{ .Name }} {{ $.Name }} = {{ .Value }}
	{{- end }}
)
func (_ {{.Name}}) I_EnumValues() []{{.Name}} {
	return []{{.Name}}{
		{{- range .Members }}
		{{ $.Name }}{{ .Name }},
		{{- end }}
	}
}
func (x {{.Name}}) String() string {
	switch x {
	{{- range .Members }}
	case {{ .Value }}:
		return "{{.Name}}"
	{{- end }}
	}
	return "Unknown"
}
{{- end -}}
`
