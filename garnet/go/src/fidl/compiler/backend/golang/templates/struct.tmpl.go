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
	_ struct{} ` + "`" + `fidl:"s,{{ .InlineSizeOld }},{{ .AlignmentOld }}" fidl_size_v1:"{{.InlineSizeV1}}" fidl_alignment_v1:"{{.AlignmentV1}}" fidl_size_v1_no_ee:"{{.InlineSizeV1}}" fidl_alignment_v1_no_ee:"{{.AlignmentV1}}"` + "`" + `
	{{- range .Members }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type -}} ` + "`" + `fidl:"{{ .FidlTag }}" fidl_offset_v1:"{{ .OffsetV1 }}" fidl_offset_v1_no_ee:"{{ .OffsetV1 }}"` + "`" + `
	{{- end }}
}

var _m{{ .Name }} = _bindings.CreateLazyMarshaler({{ .Name }}{})

func (msg *{{ .Name }}) Marshaler() _bindings.Marshaler {
	return _m{{ .Name }}
}
{{- end -}}
`
