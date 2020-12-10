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

  const std::map<uint64_t, {{ if .IsResourceType }}::fidl::UnknownData{{ else }}std::vector<uint8_t>{{ end }}>& UnknownData() const {
    return _unknown_data;
  }

  void SetUnknownDataEntry(uint32_t ordinal, {{if .IsResourceType }}::fidl::UnknownData{{ else }}std::vector<uint8_t>{{ end }}&& data) {
    auto ord = static_cast<uint64_t>(ordinal);
    ZX_ASSERT(!IsOrdinalKnown(ord));
    _unknown_data.insert({ord, std::move(data)});
  }

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

  size_t MaxOrdinal() const {
    size_t max_ordinal = static_cast<size_t>(field_presence_.MaxSetIndex()) + std::size_t{1};
    for (const auto& data : _unknown_data) {
      if (data.first > max_ordinal) {
        max_ordinal = data.first;
      }
    }
    return max_ordinal;
  }

  static bool IsOrdinalKnown(uint64_t ordinal) {
    switch (ordinal) {
  {{- range .Members }}
    case {{ .Ordinal }}:
  {{- end }}
      return true;
    default:
      return false;
    }
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
  {{- if .IsResourceType }}
  std::map<uint64_t, ::fidl::UnknownData> _unknown_data;
  {{- else }}
  std::map<uint64_t, std::vector<uint8_t>> _unknown_data;
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
  _unknown_data = std::move(other._unknown_data);
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
  _unknown_data = std::move(other._unknown_data);
  return *this;
}

bool {{ .Name }}::IsEmpty() const {
  return field_presence_.IsEmpty() && _unknown_data.size() == 0;
}

void {{ .Name }}::Encode(::fidl::Encoder* _encoder, size_t _offset) {
  size_t max_ordinal = MaxOrdinal();
  ::fidl::EncodeVectorPointer(_encoder, max_ordinal, _offset);
  if (max_ordinal == 0) return;
  size_t base = _encoder->Alloc(max_ordinal * sizeof(fidl_envelope_t));
  auto next_unknown = _unknown_data.begin();
  {{- range .Members }}
  if ({{ .FieldPresenceIsSet }}) {
    // Encode unknown fields that have an ordinal that should appear before this field.
    while (next_unknown != _unknown_data.end() && next_unknown->first < {{ .Ordinal }}) {
      size_t envelope_base = base + (next_unknown->first - 1) * sizeof(fidl_envelope_t);
    {{- if $.IsResourceType }}
      ::fidl::EncodeUnknownData(_encoder, &next_unknown->second, envelope_base);
    {{- else }}
      ::fidl::EncodeUnknownBytes(_encoder, &next_unknown->second, envelope_base);
    {{- end }}
      std::advance(next_unknown, 1);
    }

    const size_t length_before = _encoder->CurrentLength();
    const size_t handles_before = _encoder->CurrentHandleCount();
    ::fidl::Encode(
        _encoder,
        &{{ .FieldDataName }}.value,
        _encoder->Alloc(::fidl::EncodingInlineSize<{{ .Type.Decl }}, ::fidl::Encoder>(_encoder)));
    size_t envelope_base = base + ({{ .Ordinal }} - 1) * sizeof(fidl_envelope_t);
    uint64_t num_bytes_then_num_handles =
        (_encoder->CurrentLength() - length_before) |
        ((_encoder->CurrentHandleCount() - handles_before) << 32);
    ::fidl::Encode(_encoder, &num_bytes_then_num_handles, envelope_base);
    *_encoder->GetPtr<uintptr_t>(envelope_base + offsetof(fidl_envelope_t, presence)) = FIDL_ALLOC_PRESENT;
  }
  {{- end }}
  // Encode any remaining unknown fields (i.e. ones that have an ordinal outside
  // the range of known ordinals)
  for (auto curr = next_unknown; curr != _unknown_data.end(); ++curr) {
    size_t envelope_base = base + (curr->first - 1) * sizeof(fidl_envelope_t);
  {{- if .IsResourceType }}
    ::fidl::EncodeUnknownData(_encoder, &curr->second, envelope_base);
  {{- else }}
    ::fidl::EncodeUnknownBytes(_encoder, &curr->second, envelope_base);
  {{- end }}
  }
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
    size_t envelope_base = base + ({{ .Ordinal }} - 1) * sizeof(fidl_envelope_t);
    uint64_t presence;
    ::fidl::Decode(_decoder, &presence, envelope_base + offsetof(fidl_envelope_t, presence));
    if (presence != 0) {
      ::fidl::Decode(_decoder, _value->mutable_{{ .Name }}(), _decoder->GetOffset(presence));
    } else {
      _value->{{ .MethodClearName }}();
    }
  } else {
    goto done_{{ .Ordinal }};
  }
  {{- end }}

  {{/* Handle unknown data separately to avoid affecting the common case */}}
  if (count > {{ len .Members }}) {
    for (uint64_t ordinal = 1; ordinal <= count; ordinal++) {
      if (IsOrdinalKnown(ordinal))
        continue;

      size_t envelope_base = base + (ordinal - 1) * sizeof(fidl_envelope_t);
      fidl_envelope_t* envelope = _decoder->GetPtr<fidl_envelope_t>(envelope_base);
      if (envelope->presence != 0) {
        auto result = _value->_unknown_data.emplace(std::piecewise_construct, std::forward_as_tuple(ordinal), std::forward_as_tuple());
        auto iter = result.first;
    {{- if .IsResourceType }}
        iter->second.bytes.resize(envelope->num_bytes);
        iter->second.handles.resize(envelope->num_handles);
        ::fidl::DecodeUnknownDataContents(_decoder, &iter->second, _decoder->GetOffset(envelope->data));
    {{- else }}
        iter->second.resize(envelope->num_bytes);
        ::fidl::DecodeUnknownBytesContents(_decoder, &iter->second, _decoder->GetOffset(envelope->data));
    {{- end }}
      }
    }
  }

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
  return ::fidl::Clone(_unknown_data, &result->_unknown_data);
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
    return ::fidl::Equals(_lhs.UnknownData(), _rhs.UnknownData());
  }
};
{{- if .IsResourceType }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}
`
