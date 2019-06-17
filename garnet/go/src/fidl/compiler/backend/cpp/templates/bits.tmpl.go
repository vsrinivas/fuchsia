// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Bits = `
{{- define "BitsForwardDeclaration" }}
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() : value_(0u) {}
  explicit constexpr {{ .Name }}({{ .Type }} value) : value_(value) {}

  {{- range .Members }}
  const static {{ $.Name }} {{ .Name }};
  {{- end }}
  const static {{ .Name }} mask;

  explicit constexpr inline operator {{ .Type }}() const { return value_; }
  constexpr inline operator bool() const { return value_; }
  constexpr inline {{ .Name }} operator~() const;
  constexpr inline {{ .Name }} operator|(const {{ .Name }}& other) const;
  constexpr inline {{ .Name }} operator&(const {{ .Name }}& other) const;
  constexpr inline {{ .Name }} operator^(const {{ .Name }}& other) const;
  constexpr inline void operator|=(const {{ .Name }}& other);
  constexpr inline void operator&=(const {{ .Name }}& other);
  constexpr inline void operator^=(const {{ .Name }}& other);

private:
  {{ .Type }} value_;
};

{{- range $member := .Members }}
constexpr const {{ $.Namespace }}::{{ $.Name }} {{ $.Name }}::{{ $member.Name }} = {{ $.Namespace }}::{{ $.Name }}({{ $member.Value }});
{{- end }}
constexpr const {{ .Namespace }}::{{ .Name }} {{ .Name }}::mask = {{ $.Namespace }}::{{ $.Name }}({{ .Mask }}u);

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  *result = value;
  return ZX_OK;
}

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator~() const {
  return {{ $.Namespace }}::{{ $.Name }}(~this->value_ & static_cast<{{ .Type }}>(mask));
}

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator|(
    const {{ .Namespace }}::{{ .Name }}& other) const {
  return {{ $.Namespace }}::{{ $.Name }}(this->value_ | other.value_);
}

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator&(
    const {{ .Namespace }}::{{ .Name }}& other) const {
  return {{ $.Namespace }}::{{ $.Name }}(this->value_ & other.value_);
}

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator^(
    const {{ .Namespace }}::{{ .Name }}& other) const {
  return {{ $.Namespace }}::{{ $.Name }}(this->value_ ^ other.value_);
}

constexpr inline void {{ .Name }}::operator|=(
    const {{ .Namespace }}::{{ .Name }}& other) {
  this->value_ |= other.value_;
}

constexpr inline void {{ .Name }}::operator&=(
    const {{ .Namespace }}::{{ .Name }}& other) {
  this->value_ &= other.value_;
}

constexpr inline void {{ .Name }}::operator^=(
    const {{ .Namespace }}::{{ .Name }}& other) {
  this->value_ ^= other.value_;
}
{{ end }}

{{- define "BitsTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}> {
  static constexpr size_t encoded_size = sizeof({{ .Namespace }}::{{ .Name }});
  static void Encode(Encoder* encoder, {{ .Namespace }}::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = static_cast<{{ .Type }}>(*value);
    ::fidl::Encode(encoder, &underlying, offset);
  }
  static void Decode(Decoder* decoder, {{ .Namespace }}::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = {};
    ::fidl::Decode(decoder, &underlying, offset);
    *value = {{ $.Namespace }}::{{ $.Name }}(underlying);
  }
};

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}

template<>
struct Equality<{{ .Namespace }}::{{ .Name }}> {
  static inline bool Equals(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) {
    {{ .Type }} _lhs_underlying = static_cast<{{ .Type }}>(_lhs);
    {{ .Type }} _rhs_underlying = static_cast<{{ .Type }}>(_rhs);
    return Equality<{{ .Type }}>::Equals(_lhs_underlying, _rhs_underlying);
  }
};
{{- end }}
`
