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

{{ if .IsFlexible }}
	// {{ .Name }}_Unknown is the default unknown placeholder.
	{{ .Name }}_Unknown {{ .Name }} = {{ .UnknownValueForTmpl | printf "%#x" }}
{{ end }}
)

func (_ {{.Name}}) I_EnumValues() []{{.Name}} {
	return []{{.Name}}{
		{{- range .Members }}
		{{ $.Name }}{{ .Name }},
		{{- end }}
	}
}

func (_ {{.Name}}) I_EnumIsStrict() bool {
	return {{ .IsStrict }}
}

func (x {{.Name}}) IsUnknown() bool {
	switch x {
		{{- range .Members }}
		{{- if not .IsUnknown }}
		case {{ .Value }}:
			return true
		{{- end }}
		{{- end }}
		default:
			return false
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
