// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentBitsTmpl = `
{{- define "BitsForwardDeclaration" }}
{{- range .DocComments }}
//{{ . }}
{{- end }}
{{- if .IsStrict }}
// |{{ .Name }}| is strict, hence is guaranteed to only contain
// members defined in the FIDL schema.
{{- else }}
// |{{ .Name }}| is flexible, hence may contain unknown members not
// defined in the FIDL schema.
{{- end }}
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() = default;
  constexpr {{ .Name }}(const {{ .Name }}& other) = default;

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

  // Constructs an instance of |{{ .Name }}| from an underlying primitive value
  // if the primitive does not contain any unknown members not defined in the
  // FIDL schema. Otherwise, returns |fit::nullopt|.
  constexpr inline static fit::optional<{{ .Name }}> TryFrom({{ .Type }} value) {
    if (value & ~mask.value_) {
      return fit::nullopt;
    }
    return {{ .Name }}(value & {{ .Name }}::mask.value_);
  }

  // Constructs an instance of |{{ .Name }}| from an underlying primitive value,
  // clearing any bit member not defined in the FIDL schema.
  constexpr inline static {{ .Name }} TruncatingUnknown({{ .Type }} value) {
    return {{ .Name }}(value & {{ .Name }}::mask.value_);
  }

  {{- if .IsFlexible }}
  // Constructs an instance of |{{ .Name }}| from an underlying primitive value,
  // preserving any bit member not defined in the FIDL schema.
  constexpr inline static {{ .Name }} AllowingUnknown({{ .Type }} value) {
    return {{ .Name }}(value);
  }

  constexpr inline {{ .Name }} unknown_bits() const {
    return *this & {{ .Name }}(~mask.value_);
  }
  constexpr inline bool has_unknown_bits() const { return static_cast<bool>(unknown_bits()); }
  {{- end }}

private:
  constexpr explicit {{ .Name }}({{ .Type }} value) : value_(value) {}

  {{ .Type }} value_ = 0;
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
