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
	_ struct{} ` + "`" + `fidl:"s" fidl_size_v1:"{{.InlineSize}}" fidl_alignment_v1:"{{.Alignment}}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type -}} ` + "`" + `fidl:"{{ .FidlTag }}" fidl_offset_v1:"{{ .Offset }}"` + "`" + `
	{{- end }}
}

var _m{{ .Name }} = _bindings.CreateLazyMarshaler({{ .Name }}{})

func (msg *{{ .Name }}) Marshaler() _bindings.Marshaler {
	return _m{{ .Name }}
}
{{- end -}}
`
