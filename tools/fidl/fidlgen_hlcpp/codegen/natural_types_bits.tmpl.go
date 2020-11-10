// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const bitsTemplate = `
{{- define "BitsForwardDeclaration" }}
{{- if .IsStrict }}
{{- /* strict bits */}}
{{- range .DocComments }}
///{{ . }}
{{- end }}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};

const static {{ .Name }} {{ .MaskName }} = static_cast<{{ .Name }}>({{ .Mask }}u);

constexpr inline {{ .Namespace }}::{{ .Name }} operator|({{ .Namespace }}::{{ .Name }} _lhs,
                                                         {{ .Namespace }}::{{ .Name }} _rhs) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    static_cast<{{ .Type }}>(_lhs) | static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ .Namespace }}::{{ .Name }}& operator|=({{ .Namespace }}::{{ .Name }}& _lhs,
                                                           {{ .Namespace }}::{{ .Name }} _rhs) {
  _lhs = _lhs | _rhs;
  return _lhs;
}

constexpr inline {{ .Namespace }}::{{ .Name }} operator&({{ .Namespace }}::{{ .Name }} _lhs,
                                                         {{ .Namespace }}::{{ .Name }} _rhs) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    static_cast<{{ .Type }}>(_lhs) & static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ .Namespace }}::{{ .Name }}& operator&=({{ .Namespace }}::{{ .Name }}& _lhs,
                                                           {{ .Namespace }}::{{ .Name }} _rhs) {
  _lhs = _lhs & _rhs;
  return _lhs;
}

constexpr inline {{ .Namespace }}::{{ .Name }} operator^({{ .Namespace }}::{{ .Name }} _lhs,
                                                         {{ .Namespace }}::{{ .Name }} _rhs) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    static_cast<{{ .Type }}>(_lhs) ^ static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ .Namespace }}::{{ .Name }}& operator^=({{ .Namespace }}::{{ .Name }}& _lhs,
                                                           {{ .Namespace }}::{{ .Name }} _rhs) {
  _lhs = _lhs ^ _rhs;
  return _lhs;
}

constexpr inline {{ .Namespace }}::{{ .Name }} operator~({{ .Namespace }}::{{ .Name }} _value) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    ~static_cast<{{ .Type }}>(_value) & static_cast<{{ .Type }}>({{ .MaskName }}));
}

{{- else }}
{{- /* flexible bits */}}
{{- range .DocComments }}
//{{ . }}
{{- end }}
// |{{ .Name }}| is flexible, hence may contain unknown members not
// defined in the FIDL schema.
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() = default;
  constexpr {{ .Name }}(const {{ .Name }}& other) = default;

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

  // Constructs an instance of |{{ .Name }}| from an underlying primitive value,
  // preserving any bit member not defined in the FIDL schema.
  constexpr explicit {{ .Name }}({{ .Type }} value) : value_(value) {}

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

  {{- if .IsFlexible }}
  constexpr inline {{ .Name }} unknown_bits() const {
    return *this & {{ .Name }}(~kMask.value_);
  }
  constexpr inline bool has_unknown_bits() const { return static_cast<bool>(unknown_bits()); }
  {{- end }}

private:
  {{ .Type }} value_ = 0;
};

#if !(__cplusplus < 201703)

{{- range $member := .Members }}
constexpr const {{ $.Namespace }}::{{ $.Name }} {{ $.Name }}::{{ $member.Name }} = {{ $.Namespace }}::{{ $.Name }}({{ $member.Value }});
{{- end }}
constexpr const {{ .Namespace }}::{{ .Name }} {{ .Name }}::kMask = {{ $.Namespace }}::{{ $.Name }}({{ .Mask }}u);

#endif  // !(__cplusplus < 201703)

constexpr inline {{ .Namespace }}::{{ .Name }} {{ .Name }}::operator~() const {
  return {{ $.Namespace }}::{{ $.Name }}(static_cast<{{ .Type }}>(~this->value_ & kMask.value_));
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
{{- end }}

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  *result = value;
  return ZX_OK;
}

{{- end }}

{{- define "BitsDefinition" }}
{{- if .IsFlexible }}
#if (__cplusplus < 201703)

{{- range $member := .Members }}
constexpr const {{ $.Namespace }}::{{ $.Name }} {{ $.Name }}::{{ $member.Name }} = {{ $.Namespace }}::{{ $.Name }}({{ $member.Value }});
{{- end }}
constexpr const {{ .Namespace }}::{{ .Name }} {{ .Name }}::kMask = {{ $.Namespace }}::{{ $.Name }}({{ .Mask }}u);

#endif  // (__cplusplus < 201703)
{{- end }}
{{- end }}

{{- define "BitsTraits" }}
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
    {{ .Type }} _lhs_underlying = static_cast<{{ .Type }}>(_lhs);
    {{ .Type }} _rhs_underlying = static_cast<{{ .Type }}>(_rhs);
    return ::fidl::Equals(_lhs_underlying, _rhs_underlying);
  }
};
{{- end }}
`
