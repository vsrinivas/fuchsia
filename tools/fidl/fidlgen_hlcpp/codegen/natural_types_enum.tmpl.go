// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const enumTemplate = `
{{- define "EnumForwardDeclaration" }}
{{ if .IsStrict }}
{{range .DocComments}}
///{{ . }}
{{- end}}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};
{{ else }}
{{range .DocComments}}
///{{ . }}
{{- end}}
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() : value_(0) {}
  constexpr explicit {{ .Name }}({{ .Type }} value) : value_(value) {}
  constexpr {{ .Name }}(const {{ .Name }}& other) = default;
  constexpr operator {{ .Type }}() const { return value_; }

  constexpr bool IsUnknown() const {
    switch (value_) {
      {{ range .Members }}
      {{ if not .IsUnknown }}
    case {{ .Value }}:
      {{ end }}
      {{ end }}
      return false;
    default:
      return true;
    }
  }

  constexpr static {{ .Name }} Unknown() {
    return {{ .Name }}({{ .UnknownValueForTmpl | printf "%#x" }});
  }

  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  static const {{ $.Name }} {{ .Name }};
  {{- end }}

private:
  {{ .Type }} value_;
};

#if !(__cplusplus < 201703)
{{- range $member := .Members }}
constexpr const {{ $.Namespace }}::{{ $.Name }} {{ $.Name }}::{{ $member.Name }} = {{ $.Namespace }}::{{ $.Name }}({{ $member.Value }});
{{- end }}
#endif  // !(__cplusplus < 201703)

{{ end }}

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  *result = value;
  return ZX_OK;
}
{{ end }}

{{- define "EnumDefinition" }}
{{- if .IsFlexible }}
#if (__cplusplus < 201703)
{{- range $member := .Members }}
constexpr const {{ $.Namespace }}::{{ $.Name }} {{ $.Name }}::{{ $member.Name }} = {{ $.Namespace }}::{{ $.Name }}({{ $member.Value }});
{{- end }}
#endif  // (__cplusplus < 201703)
{{- end }}
{{- end }}

{{- define "EnumTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}> {
  static constexpr size_t inline_size_old = sizeof({{ .Namespace }}::{{ .Name }});
  static constexpr size_t inline_size_v1_no_ee = sizeof({{ .Namespace }}::{{ .Name }});
  static void Encode(Encoder* encoder, {{ .Namespace }}::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = static_cast<{{ .Type }}>(*value);
    ::fidl::Encode(encoder, &underlying, offset);
  }
  static void Decode(Decoder* decoder, {{ .Namespace }}::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = {};
    ::fidl::Decode(decoder, &underlying, offset);
    *value = static_cast<{{ .Namespace }}::{{ .Name }}>(underlying);
  }
};

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}
template<>
struct Equality<{{ .Namespace }}::{{ .Name }}> {
  bool operator()(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) const {
    return _lhs == _rhs;
  }
};

{{ end }}
`
