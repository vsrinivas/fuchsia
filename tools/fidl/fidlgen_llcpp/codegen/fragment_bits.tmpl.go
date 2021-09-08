// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentBitsTmpl = `
{{- define "Bits:ForwardDeclaration:Header" }}
{{ EnsureNamespace . }}
{{- .Docs }}
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
  // FIDL schema. Otherwise, returns |cpp17::nullopt|.
  constexpr inline static cpp17::optional<{{ .Name }}> TryFrom({{ .Type }} value) {
    if (value & ~kMask.value_) {
      return cpp17::nullopt;
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
constexpr const {{ $ }} {{ $.Name }}::{{ $member.Name }} =
    {{ $ }}({{ $member.Value }});
{{- end }}
constexpr const {{ . }} {{ .Name }}::kMask = {{ $ }}({{ .Mask }}u);

constexpr inline {{ . }} {{ .Name }}::operator~() const {
  return {{ $ }}(static_cast<{{ .Type }}>(~this->value_ & kMask.value_));
}

constexpr inline {{ . }} {{ .Name }}::operator|(
    const {{ . }}& other) const {
  return {{ $ }}(static_cast<{{ .Type }}>(this->value_ | other.value_));
}

constexpr inline {{ . }} {{ .Name }}::operator&(
    const {{ . }}& other) const {
  return {{ $ }}(static_cast<{{ .Type }}>(this->value_ & other.value_));
}

constexpr inline {{ . }} {{ .Name }}::operator^(
    const {{ . }}& other) const {
  return {{ $ }}(static_cast<{{ .Type }}>(this->value_ ^ other.value_));
}

constexpr inline void {{ .Name }}::operator|=(
    const {{ . }}& other) {
  this->value_ |= other.value_;
}

constexpr inline void {{ .Name }}::operator&=(
    const {{ . }}& other) {
  this->value_ &= other.value_;
}

constexpr inline void {{ .Name }}::operator^=(
    const {{ . }}& other) {
  this->value_ ^= other.value_;
}

{{ end }}

{{- define "Bits:Traits:Header" }}

template <>
struct IsFidlType<{{ . }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ . }}>);
static_assert(sizeof({{ . }}) == sizeof({{ .Type }}));
{{- end }}
`
