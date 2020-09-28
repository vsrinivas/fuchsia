// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const bitsTmpl = `
{{- define "BitsDeclaration" -}}
bitflags! {
	{{- range .DocComments}}
	///{{ . }}
	{{- end}}
	pub struct {{ .Name }}: {{ .Type }} {
		{{- range .Members }}
		{{- range .DocComments}}
		///{{ . }}
		{{- end}}
		const {{ .Name }} = {{ .Value }};
		{{- end }}
	}
}

{{- if .IsStrict}}
fidl_strict_bits! {
{{- else }}
fidl_flexible_bits! {
{{- end }}
	name: {{ .Name }},
	prim_ty: {{ .Type }},
}
{{ end }}
`
