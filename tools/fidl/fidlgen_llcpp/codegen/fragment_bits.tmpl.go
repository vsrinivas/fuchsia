// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentBitsTmpl = `
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
  explicit constexpr inline operator bool() const { return static_cast<bool>(value_); }
  constexpr inline bool operator==(const {{ .Name }}& other) const { return value_ == other.value_; }
  constexpr inline bool operator!=(const {{ .Name }}& other) const { return value_ != other.value_; }
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

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator~() const {
  return {{ $.Namespace }}::{{ $.Name }}(static_cast<{{ .Type }}>(~this->value_ & mask.value_));
}

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator|(
    const {{ .Namespace }}::{{ .Name }}& other) const {
  return {{ $.Namespace }}::{{ $.Name }}(static_cast<{{ .Type }}>(this->value_ | other.value_));
}

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator&(
    const {{ .Namespace }}::{{ .Name }}& other) const {
  return {{ $.Namespace }}::{{ $.Name }}(static_cast<{{ .Type }}>(this->value_ & other.value_));
}

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator^(
    const {{ .Namespace }}::{{ .Name }}& other) const {
  return {{ $.Namespace }}::{{ $.Name }}(static_cast<{{ .Type }}>(this->value_ ^ other.value_));
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
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
static_assert(sizeof({{ .Namespace }}::{{ .Name }}) == sizeof({{ .Type }}));
{{- end }}
`
