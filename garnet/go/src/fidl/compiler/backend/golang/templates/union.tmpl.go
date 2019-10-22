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
	{{ .TagName }} ` + "`" + `fidl:"u,{{ .InlineSizeOld }},{{ .AlignmentOld }}" fidl_size_v1_no_ee:"{{.InlineSizeV1NoEE}}" fidl_alignment_v1_no_ee:"{{.AlignmentV1NoEE}}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type }}  ` + "`" + `fidl:"{{ .FidlTag }}"` + "`" + `
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

func {{ $.Name }}With{{ .Name }}({{ .PrivateName }} {{ .Type }}) {{ $.Name }} {
	var _u {{ $.Name }}
	_u.Set{{ .Name }}({{ .PrivateName }})
	return _u
}
{{- end }}

{{- end -}}
`
