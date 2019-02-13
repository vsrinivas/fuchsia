// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const XUnion = `
{{- define "XUnionDeclaration" }}
{{- range .DocComments}}
///{{ . }}
{{- end}}
fidl_xunion! {
	name: {{ .Name }},
	members: [
	{{- range .Members }}
		{{- range .DocComments }}
		///{{ . }}
		{{- end}}
		{{ .Name }} {
			ty: {{ .Type }},
			ordinal: {{ .Ordinal }},
		},
	{{- end }}
	],
}
{{- end }}
`
