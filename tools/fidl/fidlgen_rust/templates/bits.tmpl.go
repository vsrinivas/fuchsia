// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Bits = `
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

fidl_bits! {
	name: {{ .Name }},
	prim_ty: {{ .Type }},
}
{{ end }}
`
