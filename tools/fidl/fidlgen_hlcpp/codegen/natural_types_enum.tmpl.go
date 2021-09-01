// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const enumTemplate = `
{{- define "EnumForwardDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsStrict }}
{{ .Docs }}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
    {{ .Docs }}
    {{ .Name }} = {{ .Value }},
  {{- end }}
};
{{ else }}
{{ .Docs }}
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() : value_(0) {}
  constexpr explicit {{ .Name }}({{ .Type }} value) : value_(value) {}
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
    {{ .Docs }}
    static const {{ $.Name }} {{ .Name }};
  {{- end }}

private:
  {{ .Type }} value_;
};

#if !(__cplusplus < 201703)
{{- range $member := .Members }}
constexpr const {{ $ }} {{ $.Name }}::{{ $member.Name }} = {{ $ }}({{ $member.Value }});
{{- end }}
#endif  // !(__cplusplus < 201703)

{{ end }}

inline zx_status_t Clone({{ . }} value,
                         {{ . }}* result) {
  *result = value;
  return ZX_OK;
}
{{ end }}

{{- define "EnumDefinition" }}
{{ EnsureNamespace . }}
{{- if .IsFlexible }}
#if (__cplusplus < 201703)
{{- range $member := .Members }}
constexpr const {{ $ }} {{ $.Name }}::{{ $member.Name }} = {{ $ }}({{ $member.Value }});
{{- end }}
#endif  // (__cplusplus < 201703)
{{- end }}
{{- end }}

{{- define "EnumTraits" }}
template <>
struct CodingTraits<{{ . }}> {
  static constexpr size_t inline_size_old = sizeof({{ . }});
  static constexpr size_t inline_size_v1_no_ee = sizeof({{ . }});
  static constexpr size_t inline_size_v2 = sizeof({{ . }});
  static void Encode(Encoder* encoder, {{ . }}* value, size_t offset,
                     cpp17::optional<::fidl::HandleInformation> maybe_handle_info) {
    ZX_DEBUG_ASSERT(!maybe_handle_info);
    {{ .Type }} underlying = static_cast<{{ .Type }}>(*value);
    ::fidl::Encode(encoder, &underlying, offset);
  }
  static void Decode(Decoder* decoder, {{ . }}* value, size_t offset) {
    {{ .Type }} underlying = {};
    ::fidl::Decode(decoder, &underlying, offset);
    *value = static_cast<{{ . }}>(underlying);
  }
};

inline zx_status_t Clone({{ . }} value,
                         {{ . }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}
template<>
struct Equality<{{ . }}> {
  bool operator()(const {{ . }}& _lhs, const {{ . }}& _rhs) const {
    return _lhs == _rhs;
  }
};

{{ end }}
`
