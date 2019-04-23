// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Struct = `
{{- define "StructForwardDeclaration" }}
struct {{ .Name }};
{{- end }}

{{- define "StructDeclaration" }}

{{ if .NeedsEncodeDecode -}}
extern "C" const fidl_type_t {{ .TableType }};
{{- end }}
{{range .DocComments}}
//{{ . }}
{{- end}}
struct {{ .Name }} {
  static constexpr const fidl_type_t* Type = {{ if .NeedsEncodeDecode -}} &{{ .TableType }} {{- else -}} nullptr {{- end -}};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .Size }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};

  {{- range .Members }}
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }} {{ .Name }}{};
  {{- end }}
};
{{- end }}

{{- define "StructDefinition" }}
{{- end }}

{{- define "StructTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- $struct := . }}
{{- range .Members }}
static_assert(offsetof({{ $struct.Namespace }}::{{ $struct.Name }}, {{ .Name }}) == {{ .Offset }});
{{- end }}
static_assert(sizeof({{ .Namespace }}::{{ .Name }}) == {{ .Namespace }}::{{ .Name }}::PrimarySize);
{{- end }}
`
