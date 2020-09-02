// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const unionTmpl = `
{{- define "UnionDeclaration" }}
{{- range .DocComments}}
///{{ . }}
{{- end}}
{{ .Derives }}
pub enum {{ .Name }} {
	{{- range .Members }}
	{{- range .DocComments }}
	///{{ . }}
	{{- end }}
	{{ .Name }}({{ .Type }}),
	{{- end }}
	{{- if not .Strictness }}
	#[doc(hidden)]
	__UnknownVariant {
		ordinal: u64,
		bytes: Vec<u8>,
		handles: Vec<fidl::Handle>,
	},
	{{- end }}
}

fidl_xunion! {
	name: {{ .Name }},
	members: [
	{{- range .Members }}
		{{ .Name }} {
			ty: {{ .Type }},
			ordinal: {{ .Ordinal }},
		},
	{{- end }}
	],
	{{- if not .Strictness }}
	unknown_member: __UnknownVariant,
	{{- end }}
}
{{- end }}
`
