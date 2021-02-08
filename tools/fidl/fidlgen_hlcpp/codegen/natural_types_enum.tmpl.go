// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const enumTemplate = `
{{- define "EnumForwardDeclaration" }}
{{ EnsureNamespace .Decl.Natural }}
{{ if .IsStrict }}
{{range .DocComments}}
///{{ . }}
{{- end}}
enum class {{ .Decl.Natural.Name }} : {{ .Type.Natural }} {
  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  {{ .Name }} = {{ .Value.Natural }},
  {{- end }}
};
{{ else }}
{{range .DocComments}}
///{{ . }}
{{- end}}
class {{ .Decl.Natural.Name }} final {
public:
  constexpr {{ .Decl.Natural.Name }}() : value_(0) {}
  constexpr explicit {{ .Decl.Natural.Name }}({{ .Type.Natural }} value) : value_(value) {}
  constexpr {{ .Decl.Natural.Name }}(const {{ .Decl.Natural.Name }}& other) = default;
  constexpr operator {{ .Type.Natural }}() const { return value_; }

  constexpr bool IsUnknown() const {
    switch (value_) {
      {{ range .Members }}
      {{ if not .IsUnknown }}
    case {{ .Value.Natural }}:
      {{ end }}
      {{ end }}
      return false;
    default:
      return true;
    }
  }

  constexpr static {{ .Decl.Natural.Name }} Unknown() {
    return {{ .Decl.Natural.Name }}({{ .UnknownValueForTmpl | printf "%#x" }});
  }

  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  static const {{ $.Decl.Natural.Name }} {{ .Name }};
  {{- end }}

private:
  {{ .Type.Natural }} value_;
};

#if !(__cplusplus < 201703)
{{- range $member := .Members }}
constexpr const {{ $.Decl.Natural }} {{ $.Decl.Natural.Name }}::{{ $member.Name }} = {{ $.Decl.Natural }}({{ $member.Value.Natural }});
{{- end }}
#endif  // !(__cplusplus < 201703)

{{ end }}

inline zx_status_t Clone({{ .Decl.Natural }} value,
                         {{ .Decl.Natural }}* result) {
  *result = value;
  return ZX_OK;
}
{{ end }}

{{- define "EnumDefinition" }}
{{ EnsureNamespace .Decl.Natural }}
{{- if .IsFlexible }}
#if (__cplusplus < 201703)
{{- range $member := .Members }}
constexpr const {{ $.Decl.Natural }} {{ $.Decl.Natural.Name }}::{{ $member.Name }} = {{ $.Decl.Natural }}({{ $member.Value.Natural }});
{{- end }}
#endif  // (__cplusplus < 201703)
{{- end }}
{{- end }}

{{- define "EnumTraits" }}
template <>
struct CodingTraits<{{ .Decl.Natural }}> {
  static constexpr size_t inline_size_old = sizeof({{ .Decl.Natural }});
  static constexpr size_t inline_size_v1_no_ee = sizeof({{ .Decl.Natural }});
  static void Encode(Encoder* encoder, {{ .Decl.Natural }}* value, size_t offset,
                     fit::optional<::fidl::HandleInformation> maybe_handle_info) {
    ZX_DEBUG_ASSERT(!maybe_handle_info);
    {{ .Type.Natural }} underlying = static_cast<{{ .Type.Natural }}>(*value);
    ::fidl::Encode(encoder, &underlying, offset);
  }
  static void Decode(Decoder* decoder, {{ .Decl.Natural }}* value, size_t offset) {
    {{ .Type.Natural }} underlying = {};
    ::fidl::Decode(decoder, &underlying, offset);
    *value = static_cast<{{ .Decl.Natural }}>(underlying);
  }
};

inline zx_status_t Clone({{ .Decl.Natural }} value,
                         {{ .Decl.Natural }}* result) {
  return {{ .Decl.Natural.Namespace }}::Clone(value, result);
}
template<>
struct Equality<{{ .Decl.Natural }}> {
  bool operator()(const {{ .Decl.Natural }}& _lhs, const {{ .Decl.Natural }}& _rhs) const {
    return _lhs == _rhs;
  }
};

{{ end }}
`
