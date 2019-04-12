// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Table = `
{{- define "TableDefinition" -}}
{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} struct {
	_ struct{} ` + "`" + `fidl2:"t,{{ .Size }},{{ .Alignment }}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .DataField }} {{ .Type }} {{ .Tags }}
	{{ .PresenceField }} bool
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

{{- range .Members }}

func (u *{{ $.Name }}) {{ .Setter }}({{ .PrivateDataField }} {{ .Type }}) {
	u.{{ .DataField }} = {{ .PrivateDataField }}
	u.{{ .PresenceField }} = true
}

func (u *{{ $.Name }}) {{ .Getter }}() {{ .Type }} {
	return u.{{ .DataField }}
}

func (u *{{ $.Name }}) {{ .GetterWithDefault }}(_default {{ .Type }}) {{ .Type }} {
	if !u.{{ .Haser }}() {
		return _default
	}
	return u.{{ .DataField }}
}

func (u *{{ $.Name }}) {{ .Haser }}() bool {
	return u.{{ .PresenceField }}
}

func (u *{{ $.Name }}) {{ .Clearer }}() {
	u.{{ .PresenceField }} = false
}
{{- end }}

{{- end -}}
`
