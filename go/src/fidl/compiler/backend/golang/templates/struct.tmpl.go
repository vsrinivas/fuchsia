// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDefinition" -}}
{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} struct {
	_ struct{} ` + "`" + `fidl2:"s,{{ .Size }},{{ .Alignment }}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type }} {{ .Tags }}
	{{- end }}
}

var _m{{ .Name }} = _bindings.CreateLazyMarshaler({{ .Name }}{})

func (msg *{{ .Name }}) Marshaler() _bindings.Marshaler {
	return _m{{ .Name }}
}

// Implements Payload.
func (_ *{{ .Name }}) InlineAlignment() int {
	return {{ .Alignment }}
}

// Implements Payload.
func (_ *{{ .Name }}) InlineSize() int {
	return {{ .Size }}
}
{{- end -}}
`
