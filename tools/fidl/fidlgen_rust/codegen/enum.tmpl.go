// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const enumTmpl = `
{{- define "EnumDeclaration" -}}
{{- range .DocComments}}
///{{ . }}
{{- end}}
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
{{- if .IsStrict }}
#[repr({{ .Type }})]
{{- else }}
#[non_exhaustive]
{{- end }}
pub enum {{ .Name }} {
	{{- range .Members }}
	{{- range .DocComments }}
	///{{ . }}
	{{- end }}
	{{ .Name }}{{ if $.IsStrict }} = {{ .Value }}{{ end }},
	{{- end }}
	{{- if .IsFlexible }}
	#[deprecated = "Use {{ .Name }}::unknown() to construct, {{ .Name }}Unknown!() to exhaustively match"]
	__Unknown({{ .Type }}),
	{{- end }}
}

{{- if .IsFlexible }}
/// Pattern that matches an unknown {{ .Name }} member.
#[macro_export]
macro_rules! {{ .Name }}Unknown {
	() => { _ };
}
{{- end }}

{{- if .IsStrict }}
fidl_strict_enum! {
	name: {{ .Name }},
	prim_ty: {{ .Type }},
	members: [
		{{- range .Members }}
		{{ .Name }} { value: {{ .Value }}, },
		{{- end }}
	],
}
{{- else }}
fidl_flexible_enum! {
	name: {{ .Name }},
	prim_ty: {{ .Type }},
	members: [
		{{- range .Members }}
		{{ .Name }} { value: {{ .Value }}, },
		{{- end }}
	],
	{{- range .Members }}
	{{- if .IsUnknown }}
	custom_unknown_member: {{ .Name }},
	{{- end }}
	{{- end }}
	unknown_member: __Unknown,
	default_unknown_value: {{ .UnknownValueForTmpl | printf "%#x" }},
}
{{- end }}
{{- end }}
`
