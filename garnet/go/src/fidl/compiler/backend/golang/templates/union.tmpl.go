// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionDefinition" -}}
type {{ .TagName }} uint32
const (
	_ {{ $.TagName }} = iota
	{{- range .Members }}
	{{ $.Name }}{{ .Name }}
	{{- end }}
)

{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} struct {
	{{ .TagName }} ` + "`" + `fidl:"tag" fidl2:"u,{{ .Size }},{{ .Alignment }}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type }} {{ .Tags }}
	{{- end }}
}

func (u *{{ .Name }}) Which() {{ .TagName }} {
	return u.{{ .TagName }}
}

{{- range .Members }}

func (u *{{ $.Name }}) Set{{ .Name }}({{ .PrivateName }} {{ .Type }}) {
	u.{{ $.TagName }} = {{ $.Name }}{{ .Name }}
	u.{{ .Name }} = {{ .PrivateName }}
}
{{- end }}

{{- end -}}
`
