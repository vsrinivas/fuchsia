// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Enum = `
{{- define "EnumDeclaration" -}}
{{- range .DocComments}}
///{{ . }}
{{- end}}
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr({{ .Type }})]
pub enum {{ .Name }} {
	{{- range .Members }}
	{{- range .DocComments }}
	///{{ . }}
	{{- end }}
	{{ .Name }} = {{ .Value }},
	{{- end }}
}

fidl_enum! {
	name: {{ .Name }},
	prim_ty: {{ .Type }},
	members: [
		{{- range .Members }}
		{{ .Name }} { value: {{ .Value }}, },
		{{- end }}
	],
}
{{ end }}
`
