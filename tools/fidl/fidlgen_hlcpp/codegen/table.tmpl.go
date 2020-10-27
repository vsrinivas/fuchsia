// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tableTemplate = `
{{- define "TableForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableDeclaration" }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- end }}
{{- range .DocComments }}
///{{ . }}
{{- end }}
class {{ .Name }} final {
 public:
  static const fidl_type_t* FidlType;
  /// Returns whether no field is set.
  bool IsEmpty() const;
  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  const {{ .Type.Decl }}& {{ .Name }}() const {
    ZX_ASSERT({{ .FieldPresenceIsSet }});
    return {{ .FieldDataName }}.value;
  }
  bool {{ .MethodHasName }}() const {
    return {{ .FieldPresenceIsSet }};
  }
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  {{ .Type.Decl }}* mutable_{{ .Name }}() {
    if (!{{ .FieldPresenceIsSet }}) {
      {{ .FieldPresenceSet }};
      Construct(&{{ .FieldDataName }}.value);
    }
    return &{{ .FieldDataName }}.value;
  }
  {{$.Name}}& set_{{ .Name }}({{ .Type.Decl }} _value) {
    if (!{{ .FieldPresenceIsSet }}) {
      {{ .FieldPresenceSet }};
      Construct(&{{ .FieldDataName }}.value, std::move(_value));
    } else {
      {{ .FieldDataName }}.value = std::move(_value);
    }
    return *this;
  }
  void {{ .MethodClearName }}() {
    if (!{{ .FieldPresenceIsSet }}) {
      return;
    }
    {{ .FieldPresenceClear }};
    Destruct(&{{ .FieldDataName }}.value);
  }
  {{- end }}

  {{ .Name }}();
  {{ .Name }}({{ .Name }}&& other);
  ~{{ .Name }}();
  {{ .Name }}& operator=({{ .Name }}&& other);

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* _encoder, size_t _offset);
  static void Decode(::fidl::Decoder* _decoder, {{ .Name }}* _value, size_t _offset);
  zx_status_t Clone({{ .Name }}* _result) const;

 private:
  template <class T, class... Args>
  void Construct(T* p, Args&&... args) {
    new (p) T(std::forward<Args>(args)...);
  }

  template <class T>
  void Destruct(T* p) {
    p->~T();
  }

  ::fidl::internal::BitSet<{{ .BiggestOrdinal }}> field_presence_;

  {{- range .Members }}
  {{/* The raw values of a table field are placed inside a union to ensure
       that they're not initialized (since table fields are optional by
       default). Placement new must be used to initialize the value. */ -}}
  union {{ .ValueUnionName }} {
    {{ .ValueUnionName }}() {}
    ~{{ .ValueUnionName }}() {}

    {{ .Type.Decl }} value;
  };
  {{ .ValueUnionName }} {{ .FieldDataName }};
  {{- end }}
};

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- if .IsResourceType }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}

{{- define "TableDefinition" }}
{{- if .IsResourceType }}
#ifdef __Fuchsia__
{{- end }}
extern "C" const fidl_type_t {{ .TableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .TableType }};

{{ .Name }}::{{ .Name }}() {}

{{ .Name }}::{{ .Name }}({{ .Name }}&& other) {
  field_presence_ = other.field_presence_;
  {{- range .Members }}
  if ({{ .FieldPresenceIsSet }}) {
    Construct(&{{ .FieldDataName }}.value, std::move(other.{{ .FieldDataName }}.value));
  }
  {{- end }}
}

{{ .Name }}::~{{ .Name }}() {
  {{- range .Members }}
  if ({{ .FieldPresenceIsSet }}) {
    Destruct(&{{ .FieldDataName }}.value);
  }
  {{- end }}
}

{{ .Name }}& {{ .Name }}::operator=({{ .Name }}&& other) {
  {{- range .Members }}
  if (other.{{ .FieldPresenceIsSet }}) {
    if ({{ .FieldPresenceIsSet }}) {
      {{ .FieldDataName }}.value = std::move(other.{{ .FieldDataName }}.value);
    } else {
      {{ .FieldPresenceSet }};
      Construct(&{{ .FieldDataName }}.value, std::move(other.{{ .FieldDataName }}.value));
    }
  } else if ({{ .FieldPresenceIsSet }}) {
    {{ .FieldPresenceClear }};
    Destruct(&{{ .FieldDataName }}.value);
  }
  {{- end }}
  return *this;
}

bool {{ .Name }}::IsEmpty() const {
  return field_presence_.IsEmpty();
}

void {{ .Name }}::Encode(::fidl::Encoder* _encoder, size_t _offset) {
  size_t max_ordinal = field_presence_.MaxSetIndex() + 1ll;
  ::fidl::EncodeVectorPointer(_encoder, max_ordinal, _offset);
  if (max_ordinal == 0) return;
  {{- if len .Members }}
  size_t base = _encoder->Alloc(max_ordinal * 2 * sizeof(uint64_t));
  {{- end }}
  {{- range .Members }}
  if ({{ .FieldPresenceIsSet }}) {
    const size_t length_before = _encoder->CurrentLength();
    const size_t handles_before = _encoder->CurrentHandleCount();
    ::fidl::Encode(
        _encoder,
        &{{ .FieldDataName }}.value,
        _encoder->Alloc(::fidl::EncodingInlineSize<{{ .Type.Decl }}, ::fidl::Encoder>(_encoder)));
    size_t envelope_base = base + ({{ .Ordinal }} - 1) * 2 * sizeof(uint64_t);
    uint64_t num_bytes_then_num_handles =
        (_encoder->CurrentLength() - length_before) |
        ((_encoder->CurrentHandleCount() - handles_before) << 32);
    ::fidl::Encode(_encoder, &num_bytes_then_num_handles, envelope_base);
    *_encoder->GetPtr<uintptr_t>(envelope_base + sizeof(uint64_t)) = FIDL_ALLOC_PRESENT;
  }
  {{- end }}
}

void {{ .Name }}::Decode(::fidl::Decoder* _decoder, {{ .Name }}* _value, size_t _offset) {
  fidl_vector_t* encoded = _decoder->GetPtr<fidl_vector_t>(_offset);
  size_t base;
  size_t count;
  if (!encoded->data) {
    goto clear_all;
  }

  base = _decoder->GetOffset(encoded->data);
  count = encoded->count;

  {{- range .Members }}
  if (count >= {{ .Ordinal }}) {
    size_t envelope_base = base + ({{ .Ordinal }} - 1) * 2 * sizeof(uint64_t);
    uint64_t presence;
    ::fidl::Decode(_decoder, &presence, envelope_base + sizeof(uint64_t));
    if (presence != 0) {
      ::fidl::Decode(_decoder, _value->mutable_{{ .Name }}(), _decoder->GetOffset(presence));
    } else {
      _value->{{ .MethodClearName }}();
    }
  } else {
    goto done_{{ .Ordinal }};
  }
  {{- end }}

  return;

  // Clear unset values.
clear_all:
  {{- range .Members }}
done_{{ .Ordinal }}:
  _value->{{ .MethodClearName }}();
  {{- end }}
  return;
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  {{- range .Members }}
  if ({{ .FieldPresenceIsSet }}) {
    zx_status_t _status = ::fidl::Clone({{ .FieldDataName }}.value, result->mutable_{{ .Name }}());
    if (_status != ZX_OK)
      return _status;
  } else {
    result->{{ .MethodClearName }}();
  }
  {{- end }}
  return ZX_OK;
}
{{- if .IsResourceType }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}

{{- define "TableTraits" }}
{{- if .IsResourceType }}
#ifdef __Fuchsia__
{{- end }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .InlineSize }}> {};

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& _value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return _value.Clone(result);
}
template<>
struct Equality<{{ .Namespace }}::{{ .Name }}> {
  bool operator()(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) const {
    {{- range .Members }}
    if (_lhs.{{ .MethodHasName }}()) {
      if (!_rhs.{{ .MethodHasName }}()) {
	return false;
      }
      if (!::fidl::Equals(_lhs.{{ .Name }}(), _rhs.{{ .Name }}())) {
	return false;
      }
    } else if (_rhs.{{ .MethodHasName }}()) {
      return false;
    }
    {{- end }}
    return true;
  }
};
{{- if .IsResourceType }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}
`
