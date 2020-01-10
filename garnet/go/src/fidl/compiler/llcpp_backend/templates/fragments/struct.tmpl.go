// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Struct = `
{{- define "StructForwardDeclaration" }}
struct {{ .Name }};
{{- end }}

{{- define "StructDeclaration" }}

extern "C" const fidl_type_t {{ .TableType }};
extern "C" const fidl_type_t {{ .V1TableType }};
{{range .DocComments}}
//{{ . }}
{{- end}}
struct {{ .Name }} {
  static constexpr const fidl_type_t* Type = &{{ .V1TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSizeV1NoEE }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLineV1NoEE }};
  static constexpr bool HasPointer = {{ .HasPointer }};

  {{- range .Members }}
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }} {{ .Name }} = {};
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
static_assert(offsetof({{ $struct.Namespace }}::{{ $struct.Name }}, {{ .Name }}) == {{ .OffsetV1NoEE }});
{{- end }}
static_assert(sizeof({{ .Namespace }}::{{ .Name }}) == {{ .Namespace }}::{{ .Name }}::PrimarySize);
{{- end }}
`
