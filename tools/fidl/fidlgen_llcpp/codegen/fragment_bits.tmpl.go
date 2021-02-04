// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentBitsTmpl = `
{{- define "BitsForwardDeclaration" }}
namespace wire {
{{- range .DocComments }}
//{{ . }}
{{- end }}
{{- if .IsStrict }}
// |{{ .Name }}| is strict, hence is guaranteed to only contain
// members defined in the FIDL schema when receiving it in a message.
// Sending unknown members will fail at runtime.
{{- else }}
// |{{ .Name }}| is flexible, hence may contain unknown members not
// defined in the FIDL schema.
{{- end }}
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() = default;
  constexpr {{ .Name }}(const {{ .Name }}& other) = default;

  // Constructs an instance of |{{ .Name }}| from an underlying primitive value,
  // preserving any bit member not defined in the FIDL schema.
  explicit constexpr {{ .Name }}({{ .Type }} value) : value_(value) {}

  {{- range .Members }}
  const static {{ $.Name }} {{ .Name }};
  {{- end }}
  const static {{ .Name }} kMask;

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
    if (value & ~kMask.value_) {
      return fit::nullopt;
    }
    return {{ .Name }}(value & {{ .Name }}::kMask.value_);
  }

  // Constructs an instance of |{{ .Name }}| from an underlying primitive value,
  // clearing any bit member not defined in the FIDL schema.
  constexpr inline static {{ .Name }} TruncatingUnknown({{ .Type }} value) {
    return {{ .Name }}(value & {{ .Name }}::kMask.value_);
  }

  {{- if .IsFlexible }}
  constexpr inline {{ .Name }} unknown_bits() const {
    return *this & {{ .Name }}(~kMask.value_);
  }
  constexpr inline bool has_unknown_bits() const { return static_cast<bool>(unknown_bits()); }
  {{- end }}

private:
  {{ .Type }} value_ = 0;
};

{{- range $member := .Members }}
constexpr const {{ $.Namespace }}::wire::{{ $.Name }} {{ $.Name }}::{{ $member.Name }} = {{ $.Namespace }}::wire::{{ $.Name }}({{ $member.Value }});
{{- end }}
constexpr const {{ .Namespace }}::wire::{{ .Name }} {{ .Name }}::kMask = {{ $.Namespace }}::wire::{{ $.Name }}({{ .Mask }}u);

constexpr inline {{ .Namespace }}::wire::{{ .Name }} {{ .Name }}::operator~() const {
  return {{ $.Namespace }}::wire::{{ $.Name }}(static_cast<{{ .Type }}>(~this->value_ & kMask.value_));
}

constexpr inline {{ .Namespace }}::wire::{{ .Name }} {{ .Name }}::operator|(
    const {{ .Namespace }}::wire::{{ .Name }}& other) const {
  return {{ $.Namespace }}::wire::{{ $.Name }}(static_cast<{{ .Type }}>(this->value_ | other.value_));
}

constexpr inline {{ .Namespace }}::wire::{{ .Name }} {{ .Name }}::operator&(
    const {{ .Namespace }}::wire::{{ .Name }}& other) const {
  return {{ $.Namespace }}::wire::{{ $.Name }}(static_cast<{{ .Type }}>(this->value_ & other.value_));
}

constexpr inline {{ .Namespace }}::wire::{{ .Name }} {{ .Name }}::operator^(
    const {{ .Namespace }}::wire::{{ .Name }}& other) const {
  return {{ $.Namespace }}::wire::{{ $.Name }}(static_cast<{{ .Type }}>(this->value_ ^ other.value_));
}

constexpr inline void {{ .Name }}::operator|=(
    const {{ .Namespace }}::wire::{{ .Name }}& other) {
  this->value_ |= other.value_;
}

constexpr inline void {{ .Name }}::operator&=(
    const {{ .Namespace }}::wire::{{ .Name }}& other) {
  this->value_ &= other.value_;
}

constexpr inline void {{ .Name }}::operator^=(
    const {{ .Namespace }}::wire::{{ .Name }}& other) {
  this->value_ ^= other.value_;
}

}  // namespace wire

using {{ .Name }} = wire::{{ .Name }};
{{ end }}

{{- define "BitsTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::wire::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::wire::{{ .Name }}>);
static_assert(sizeof({{ .Namespace }}::wire::{{ .Name }}) == sizeof({{ .Type }}));
{{- end }}
`
