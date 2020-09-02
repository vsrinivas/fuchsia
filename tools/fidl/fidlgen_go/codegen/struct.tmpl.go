// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const structTmpl = `
{{- define "StructDefinition" -}}
{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} struct {
	_ struct{} ` + "`{{.Tags}}`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type -}} ` + "`{{.Tags}}`" + `
	{{- end }}
}

var _m{{ .Name }} = _bindings.CreateLazyMarshaler({{ .Name }}{})

func (msg *{{ .Name }}) Marshaler() _bindings.Marshaler {
	return _m{{ .Name }}
}
{{- end -}}
`
