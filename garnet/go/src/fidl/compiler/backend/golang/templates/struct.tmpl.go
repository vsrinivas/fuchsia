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
	_ struct{} ` + "`" + `fidl:"s,{{ .InlineSizeOld }},{{ .AlignmentOld }}" fidl_size_v1_no_ee:"{{.InlineSizeV1NoEE}}" fidl_alignment_v1_no_ee:"{{.AlignmentV1NoEE}}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type -}} {{ .Tags }}
	{{- end }}
}

var _m{{ .Name }} = _bindings.CreateLazyMarshaler({{ .Name }}{})

func (msg *{{ .Name }}) Marshaler() _bindings.Marshaler {
	return _m{{ .Name }}
}
{{- end -}}
`
