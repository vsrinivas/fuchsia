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
	{{- if .IsFlexible }}
	#[deprecated = "Use ` + "`{{ .Name }}::unknown()` to construct and `{{ .Name }}Unknown!()`" + ` to exhaustively match."]
	#[doc(hidden)]
	__Unknown {
		ordinal: u64,
		{{- if .IsResourceType }}
		data: fidl::UnknownData,
		{{- else }}
		bytes: Vec<u8>,
		{{- end }}
	},
	{{- end }}
}

{{- if .IsFlexible }}
/// Pattern that matches an unknown {{ .Name }} member.
#[macro_export]
macro_rules! {{ .Name }}Unknown {
	() => { _ };
}
{{- end }}
fidl_xunion! {
	name: {{ .Name }},
	members: [
	{{- range .Members }}
		{{ .Name }} {
			ty: {{ .Type }},
			ordinal: {{ .Ordinal }},
			{{- if .HasHandleMetadata }}
			handle_metadata: {
				handle_subtype: {{ .HandleSubtype }},
				handle_rights: {{ .HandleRights }},
			},
			{{- end }}
		},
	{{- end }}
	],
	{{- if .IsStrict }}
	strict_{{ if .IsResourceType }}resource{{ else }}value{{ end }}: true,
	{{- else }}
	{{ if .IsResourceType }}resource{{ else }}value{{ end }}_unknown_member: __Unknown,
	{{- end }}
}
{{- end }}
`
