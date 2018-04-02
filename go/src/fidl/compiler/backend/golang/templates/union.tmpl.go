// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionDefinition" -}}
type {{ .TagName }} uint32
const (
	{{- range .Members }}
	{{ $.Name }}{{ .Name }} {{ $.TagName }} = iota
	{{- end }}
)

// {{ .Name }} is a FIDL union.
type {{ .Name }} struct {
	{{ .TagName }} ` + "`fidl:\"tag\"`" + `
	{{- range .Members }}
	{{ .Name }} {{ .Type }} {{ .Tag }}
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
