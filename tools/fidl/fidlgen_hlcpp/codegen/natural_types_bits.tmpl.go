// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const bitsTemplate = `
{{- define "BitsForwardDeclaration" }}
{{ EnsureNamespace . }}
{{- if .IsStrict }}
{{- /* strict bits */}}
{{- .Docs }}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{ .Docs }}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};

const static {{ .Name }} {{ .MaskName.Name }} = static_cast<{{ .Name }}>({{ .Mask }}u);

constexpr inline {{ . }} operator|({{ . }} _lhs, {{ . }} _rhs) {
  return static_cast<{{ . }}>(
    static_cast<{{ .Type }}>(_lhs) | static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ . }}& operator|=({{ . }}& _lhs,
                                                           {{ . }} _rhs) {
  _lhs = _lhs | _rhs;
  return _lhs;
}

constexpr inline {{ . }} operator&({{ . }} _lhs,
                                                         {{ . }} _rhs) {
  return static_cast<{{ . }}>(
    static_cast<{{ .Type }}>(_lhs) & static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ . }}& operator&=({{ . }}& _lhs,
                                                           {{ . }} _rhs) {
  _lhs = _lhs & _rhs;
  return _lhs;
}

constexpr inline {{ . }} operator^({{ . }} _lhs,
                                                         {{ . }} _rhs) {
  return static_cast<{{ . }}>(
    static_cast<{{ .Type }}>(_lhs) ^ static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ . }}& operator^=({{ . }}& _lhs,
                                                           {{ . }} _rhs) {
  _lhs = _lhs ^ _rhs;
  return _lhs;
}

constexpr inline {{ . }} operator~({{ . }} _value) {
  return static_cast<{{ . }}>(
    ~static_cast<{{ .Type }}>(_value) & static_cast<{{ .Type }}>({{ .MaskName }}));
}

{{- else }}
{{- /* flexible bits */}}
{{- .Docs }}
// |{{ .Name }}| is flexible, hence may contain unknown members not
// defined in the FIDL schema.
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() = default;
  constexpr {{ .Name }}(const {{ .Name }}& other) = default;

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
constexpr const {{ $ }} {{ $.Name }}::{{ $member.Name }} = {{ $ }}({{ $member.Value }});
{{- end }}
constexpr const {{ . }} {{ .Name }}::kMask = {{ $ }}({{ .Mask }}u);

#endif  // !(__cplusplus < 201703)

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
{{- end }}

inline zx_status_t Clone({{ . }} value,
                         {{ . }}* result) {
  *result = value;
  return ZX_OK;
}

{{- end }}

{{- define "BitsDefinition" }}
{{ EnsureNamespace . }}
{{- if .IsFlexible }}
#if (__cplusplus < 201703)

{{- range $member := .Members }}
constexpr const {{ $ }} {{ $.Name }}::{{ $member.Name }} = {{ $ }}({{ $member.Value }});
{{- end }}
constexpr const {{ . }} {{ .Name }}::kMask = {{ $ }}({{ .Mask }}u);

#endif  // (__cplusplus < 201703)
{{- end }}
{{- end }}

{{- define "BitsTraits" }}
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
    {{ .Type }} _lhs_underlying = static_cast<{{ .Type }}>(_lhs);
    {{ .Type }} _rhs_underlying = static_cast<{{ .Type }}>(_rhs);
    return ::fidl::Equals(_lhs_underlying, _rhs_underlying);
  }
};
{{- end }}
`
