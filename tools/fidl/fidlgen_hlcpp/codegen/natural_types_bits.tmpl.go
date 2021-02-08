// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const bitsTemplate = `
{{- define "BitsForwardDeclaration" }}
{{ EnsureNamespace .Decl.Natural }}
{{- if .IsStrict }}
{{- /* strict bits */}}
{{- range .DocComments }}
///{{ . }}
{{- end }}
enum class {{ .Decl.Natural.Name }} : {{ .Type.Natural }} {
  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  {{ .Name }} = {{ .Value.Natural }},
  {{- end }}
};

const static {{ .Decl.Natural.Name }} {{ .MaskName.Natural.Name }} = static_cast<{{ .Decl.Natural.Name }}>({{ .Mask }}u);

constexpr inline {{ .Decl.Natural }} operator|({{ .Decl.Natural }} _lhs,
                                                         {{ .Decl.Natural }} _rhs) {
  return static_cast<{{ .Decl.Natural }}>(
    static_cast<{{ .Type.Natural }}>(_lhs) | static_cast<{{ .Type.Natural }}>(_rhs));
}

constexpr inline {{ .Decl.Natural }}& operator|=({{ .Decl.Natural }}& _lhs,
                                                           {{ .Decl.Natural }} _rhs) {
  _lhs = _lhs | _rhs;
  return _lhs;
}

constexpr inline {{ .Decl.Natural }} operator&({{ .Decl.Natural }} _lhs,
                                                         {{ .Decl.Natural }} _rhs) {
  return static_cast<{{ .Decl.Natural }}>(
    static_cast<{{ .Type.Natural }}>(_lhs) & static_cast<{{ .Type.Natural }}>(_rhs));
}

constexpr inline {{ .Decl.Natural }}& operator&=({{ .Decl.Natural }}& _lhs,
                                                           {{ .Decl.Natural }} _rhs) {
  _lhs = _lhs & _rhs;
  return _lhs;
}

constexpr inline {{ .Decl.Natural }} operator^({{ .Decl.Natural }} _lhs,
                                                         {{ .Decl.Natural }} _rhs) {
  return static_cast<{{ .Decl.Natural }}>(
    static_cast<{{ .Type.Natural }}>(_lhs) ^ static_cast<{{ .Type.Natural }}>(_rhs));
}

constexpr inline {{ .Decl.Natural }}& operator^=({{ .Decl.Natural }}& _lhs,
                                                           {{ .Decl.Natural }} _rhs) {
  _lhs = _lhs ^ _rhs;
  return _lhs;
}

constexpr inline {{ .Decl.Natural }} operator~({{ .Decl.Natural }} _value) {
  return static_cast<{{ .Decl.Natural }}>(
    ~static_cast<{{ .Type.Natural }}>(_value) & static_cast<{{ .Type.Natural }}>({{ .MaskName.Natural }}));
}

{{- else }}
{{- /* flexible bits */}}
{{- range .DocComments }}
//{{ . }}
{{- end }}
// |{{ .Decl.Natural.Name }}| is flexible, hence may contain unknown members not
// defined in the FIDL schema.
class {{ .Decl.Natural.Name }} final {
public:
  constexpr {{ .Decl.Natural.Name }}() = default;
  constexpr {{ .Decl.Natural.Name }}(const {{ .Decl.Natural.Name }}& other) = default;

  // Constructs an instance of |{{ .Decl.Natural.Name }}| from an underlying primitive value
  // if the primitive does not contain any unknown members not defined in the
  // FIDL schema. Otherwise, returns |fit::nullopt|.
  constexpr inline static fit::optional<{{ .Decl.Natural.Name }}> TryFrom({{ .Type.Natural }} value) {
    if (value & ~kMask.value_) {
      return fit::nullopt;
    }
    return {{ .Decl.Natural.Name }}(value & {{ .Decl.Natural.Name }}::kMask.value_);
  }

  // Constructs an instance of |{{ .Decl.Natural.Name }}| from an underlying primitive value,
  // clearing any bit member not defined in the FIDL schema.
  constexpr inline static {{ .Decl.Natural.Name }} TruncatingUnknown({{ .Type.Natural }} value) {
    return {{ .Decl.Natural.Name }}(value & {{ .Decl.Natural.Name }}::kMask.value_);
  }

  // Constructs an instance of |{{ .Decl.Natural.Name }}| from an underlying primitive value,
  // preserving any bit member not defined in the FIDL schema.
  constexpr explicit {{ .Decl.Natural.Name }}({{ .Type.Natural }} value) : value_(value) {}

  {{- range .Members }}
  const static {{ $.Decl.Natural.Name }} {{ .Name }};
  {{- end }}
  const static {{ .Decl.Natural.Name }} kMask;

  explicit constexpr inline operator {{ .Type.Natural }}() const { return value_; }
  explicit constexpr inline operator bool() const { return static_cast<bool>(value_); }
  constexpr inline bool operator==(const {{ .Decl.Natural.Name }}& other) const { return value_ == other.value_; }
  constexpr inline bool operator!=(const {{ .Decl.Natural.Name }}& other) const { return value_ != other.value_; }
  constexpr inline {{ .Decl.Natural.Name }} operator~() const;
  constexpr inline {{ .Decl.Natural.Name }} operator|(const {{ .Decl.Natural.Name }}& other) const;
  constexpr inline {{ .Decl.Natural.Name }} operator&(const {{ .Decl.Natural.Name }}& other) const;
  constexpr inline {{ .Decl.Natural.Name }} operator^(const {{ .Decl.Natural.Name }}& other) const;
  constexpr inline void operator|=(const {{ .Decl.Natural.Name }}& other);
  constexpr inline void operator&=(const {{ .Decl.Natural.Name }}& other);
  constexpr inline void operator^=(const {{ .Decl.Natural.Name }}& other);

  {{- if .IsFlexible }}
  constexpr inline {{ .Decl.Natural.Name }} unknown_bits() const {
    return *this & {{ .Decl.Natural.Name }}(~kMask.value_);
  }
  constexpr inline bool has_unknown_bits() const { return static_cast<bool>(unknown_bits()); }
  {{- end }}

private:
  {{ .Type.Natural }} value_ = 0;
};

#if !(__cplusplus < 201703)

{{- range $member := .Members }}
constexpr const {{ $.Decl.Natural }} {{ $.Decl.Natural.Name }}::{{ $member.Name }} = {{ $.Decl.Natural }}({{ $member.Value.Natural }});
{{- end }}
constexpr const {{ .Decl.Natural }} {{ .Decl.Natural.Name }}::kMask = {{ $.Decl.Natural }}({{ .Mask }}u);

#endif  // !(__cplusplus < 201703)

constexpr inline {{ .Decl.Natural }} {{ .Decl.Natural.Name }}::operator~() const {
  return {{ $.Decl.Natural }}(static_cast<{{ .Type.Natural }}>(~this->value_ & kMask.value_));
}

constexpr inline {{ .Decl.Natural }} {{ .Decl.Natural.Name }}::operator|(
    const {{ .Decl.Natural }}& other) const {
  return {{ $.Decl.Natural }}(static_cast<{{ .Type.Natural }}>(this->value_ | other.value_));
}

constexpr inline {{ .Decl.Natural }} {{ .Decl.Natural.Name }}::operator&(
    const {{ .Decl.Natural }}& other) const {
  return {{ $.Decl.Natural }}(static_cast<{{ .Type.Natural }}>(this->value_ & other.value_));
}

constexpr inline {{ .Decl.Natural }} {{ .Decl.Natural.Name }}::operator^(
    const {{ .Decl.Natural }}& other) const {
  return {{ $.Decl.Natural }}(static_cast<{{ .Type.Natural }}>(this->value_ ^ other.value_));
}

constexpr inline void {{ .Decl.Natural.Name }}::operator|=(
    const {{ .Decl.Natural }}& other) {
  this->value_ |= other.value_;
}

constexpr inline void {{ .Decl.Natural.Name }}::operator&=(
    const {{ .Decl.Natural }}& other) {
  this->value_ &= other.value_;
}

constexpr inline void {{ .Decl.Natural.Name }}::operator^=(
    const {{ .Decl.Natural }}& other) {
  this->value_ ^= other.value_;
}
{{- end }}

inline zx_status_t Clone({{ .Decl.Natural }} value,
                         {{ .Decl.Natural }}* result) {
  *result = value;
  return ZX_OK;
}

{{- end }}

{{- define "BitsDefinition" }}
{{ EnsureNamespace .Decl.Natural }}
{{- if .IsFlexible }}
#if (__cplusplus < 201703)

{{- range $member := .Members }}
constexpr const {{ $.Decl.Natural }} {{ $.Decl.Natural.Name }}::{{ $member.Name }} = {{ $.Decl.Natural }}({{ $member.Value.Natural }});
{{- end }}
constexpr const {{ .Decl.Natural }} {{ .Decl.Natural.Name }}::kMask = {{ $.Decl.Natural }}({{ .Mask }}u);

#endif  // (__cplusplus < 201703)
{{- end }}
{{- end }}

{{- define "BitsTraits" }}
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
    {{ .Type.Natural }} _lhs_underlying = static_cast<{{ .Type.Natural }}>(_lhs);
    {{ .Type.Natural }} _rhs_underlying = static_cast<{{ .Type.Natural }}>(_rhs);
    return ::fidl::Equals(_lhs_underlying, _rhs_underlying);
  }
};
{{- end }}
`
