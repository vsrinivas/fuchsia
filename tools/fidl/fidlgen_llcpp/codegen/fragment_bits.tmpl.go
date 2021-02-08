// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentBitsTmpl = `
{{- define "BitsForwardDeclaration" }}
{{ EnsureNamespace .Decl.Wire }}
{{- range .DocComments }}
//{{ . }}
{{- end }}
{{- if .IsStrict }}
// |{{ .Decl.Wire.Name }}| is strict, hence is guaranteed to only contain
// members defined in the FIDL schema when receiving it in a message.
// Sending unknown members will fail at runtime.
{{- else }}
// |{{ .Decl.Wire.Name }}| is flexible, hence may contain unknown members not
// defined in the FIDL schema.
{{- end }}
class {{ .Decl.Wire.Name }} final {
public:
  constexpr {{ .Decl.Wire.Name }}() = default;
  constexpr {{ .Decl.Wire.Name }}(const {{ .Decl.Wire.Name }}& other) = default;

  // Constructs an instance of |{{ .Decl.Wire.Name }}| from an underlying primitive value,
  // preserving any bit member not defined in the FIDL schema.
  explicit constexpr {{ .Decl.Wire.Name }}({{ .Type.Wire }} value) : value_(value) {}

  {{- range .Members }}
  const static {{ $.Decl.Wire.Name }} {{ .Name }};
  {{- end }}
  const static {{ .Decl.Wire.Name }} kMask;

  explicit constexpr inline operator {{ .Type.Wire }}() const { return value_; }
  explicit constexpr inline operator bool() const { return static_cast<bool>(value_); }
  constexpr inline bool operator==(const {{ .Decl.Wire.Name }}& other) const { return value_ == other.value_; }
  constexpr inline bool operator!=(const {{ .Decl.Wire.Name }}& other) const { return value_ != other.value_; }
  constexpr inline {{ .Decl.Wire.Name }} operator~() const;
  constexpr inline {{ .Decl.Wire.Name }} operator|(const {{ .Decl.Wire.Name }}& other) const;
  constexpr inline {{ .Decl.Wire.Name }} operator&(const {{ .Decl.Wire.Name }}& other) const;
  constexpr inline {{ .Decl.Wire.Name }} operator^(const {{ .Decl.Wire.Name }}& other) const;
  constexpr inline void operator|=(const {{ .Decl.Wire.Name }}& other);
  constexpr inline void operator&=(const {{ .Decl.Wire.Name }}& other);
  constexpr inline void operator^=(const {{ .Decl.Wire.Name }}& other);

  // Constructs an instance of |{{ .Decl.Wire.Name }}| from an underlying primitive value
  // if the primitive does not contain any unknown members not defined in the
  // FIDL schema. Otherwise, returns |fit::nullopt|.
  constexpr inline static fit::optional<{{ .Decl.Wire.Name }}> TryFrom({{ .Type.Wire }} value) {
    if (value & ~kMask.value_) {
      return fit::nullopt;
    }
    return {{ .Decl.Wire.Name }}(value & {{ .Decl.Wire.Name }}::kMask.value_);
  }

  // Constructs an instance of |{{ .Decl.Wire.Name }}| from an underlying primitive value,
  // clearing any bit member not defined in the FIDL schema.
  constexpr inline static {{ .Decl.Wire.Name }} TruncatingUnknown({{ .Type.Wire }} value) {
    return {{ .Decl.Wire.Name }}(value & {{ .Decl.Wire.Name }}::kMask.value_);
  }

  {{- if .IsFlexible }}
  constexpr inline {{ .Decl.Wire.Name }} unknown_bits() const {
    return *this & {{ .Decl.Wire.Name }}(~kMask.value_);
  }
  constexpr inline bool has_unknown_bits() const { return static_cast<bool>(unknown_bits()); }
  {{- end }}

private:
  {{ .Type.Wire }} value_ = 0;
};

{{- range $member := .Members }}
constexpr const {{ $.Decl.Wire }} {{ $.Decl.Wire.Name }}::{{ $member.Name }} =
    {{ $.Decl.Wire }}({{ $member.Value.Wire }});
{{- end }}
constexpr const {{ .Decl.Wire }} {{ .Decl.Wire.Name }}::kMask = {{ $.Decl.Wire }}({{ .Mask }}u);

constexpr inline {{ .Decl.Wire }} {{ .Decl.Wire.Name }}::operator~() const {
  return {{ $.Decl.Wire }}(static_cast<{{ .Type.Wire }}>(~this->value_ & kMask.value_));
}

constexpr inline {{ .Decl.Wire }} {{ .Decl.Wire.Name }}::operator|(
    const {{ .Decl.Wire }}& other) const {
  return {{ $.Decl.Wire }}(static_cast<{{ .Type.Wire }}>(this->value_ | other.value_));
}

constexpr inline {{ .Decl.Wire }} {{ .Decl.Wire.Name }}::operator&(
    const {{ .Decl.Wire }}& other) const {
  return {{ $.Decl.Wire }}(static_cast<{{ .Type.Wire }}>(this->value_ & other.value_));
}

constexpr inline {{ .Decl.Wire }} {{ .Decl.Wire.Name }}::operator^(
    const {{ .Decl.Wire }}& other) const {
  return {{ $.Decl.Wire }}(static_cast<{{ .Type.Wire }}>(this->value_ ^ other.value_));
}

constexpr inline void {{ .Decl.Wire.Name }}::operator|=(
    const {{ .Decl.Wire }}& other) {
  this->value_ |= other.value_;
}

constexpr inline void {{ .Decl.Wire.Name }}::operator&=(
    const {{ .Decl.Wire }}& other) {
  this->value_ &= other.value_;
}

constexpr inline void {{ .Decl.Wire.Name }}::operator^=(
    const {{ .Decl.Wire }}& other) {
  this->value_ ^= other.value_;
}

{{- EnsureNamespace .WireAlias }}
using {{ .WireAlias.Name }} = {{ .Decl.Wire }};
{{ end }}

{{- define "BitsTraits" }}

template <>
struct IsFidlType<{{ .Decl.Wire }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Decl.Wire }}>);
static_assert(sizeof({{ .Decl.Wire }}) == sizeof({{ .Type.Wire }}));
{{- end }}
`
