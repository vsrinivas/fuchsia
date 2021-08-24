// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tableTemplate = `
{{- define "TableForwardDeclaration" }}
{{ EnsureNamespace . }}
class {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
{{- .Docs }}
class {{ .Name }} final {
 public:
  static const fidl_type_t* FidlType;
  /// Returns whether no field is set.
  bool IsEmpty() const;
  {{- range .Members }}
  {{ .Docs }}
  const {{ .Type }}& {{ .Name }}() const {
    ZX_ASSERT({{ .FieldPresenceIsSet }});
    return {{ .FieldDataName }}.value;
  }
  bool {{ .MethodHasName }}() const {
    return {{ .FieldPresenceIsSet }};
  }
  {{ .Docs }}
  {{ .Type }}* mutable_{{ .Name }}() {
    if (!{{ .FieldPresenceIsSet }}) {
      {{ .FieldPresenceSet }};
      Construct(&{{ .FieldDataName }}.value);
    }
    return &{{ .FieldDataName }}.value;
  }
  {{ $.Name }}& set_{{ .Name }}({{ .Type }} _value) {
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

  void SetUnknownDataEntry(uint32_t ordinal, {{ if .IsResourceType }}::fidl::UnknownData{{ else }}std::vector<uint8_t>{{ end }}&& data) {
    auto ord = static_cast<uint64_t>(ordinal);
    ZX_ASSERT(!IsOrdinalKnown(ord));
    _unknown_data.insert({ord, std::move(data)});
  }

  {{ .Name }}();
  {{ .Name }}({{ .Name }}&& other);
  ~{{ .Name }}();
  {{ .Name }}& operator=({{ .Name }}&& other);

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* _encoder, size_t _offset,
              cpp17::optional<::fidl::HandleInformation> maybe_handle_info = cpp17::nullopt);
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

    {{ .Type }} value;
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
{{- EndifFuchsia -}}
{{ end }}

{{- end }}

{{- define "TableDefinition" }}
{{ EnsureNamespace . }}
{{- if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
extern "C" const fidl_type_t {{ .CodingTableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .CodingTableType }};

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

void {{ .Name }}::Encode(::fidl::Encoder* _encoder, size_t _offset,
                         cpp17::optional<::fidl::HandleInformation> maybe_handle_info) {
  size_t max_ordinal = MaxOrdinal();
  ::fidl::EncodeVectorPointer(_encoder, max_ordinal, _offset);
  if (max_ordinal == 0) return;
  size_t envelope_size = (_encoder->wire_format() == ::fidl::internal::WireFormatVersion::kV1) ?
    sizeof(fidl_envelope_t) : sizeof(fidl_envelope_v2_t);
  size_t base = _encoder->Alloc(max_ordinal * envelope_size);
  auto next_unknown = _unknown_data.begin();
  {{- range .Members }}
  if ({{ .FieldPresenceIsSet }}) {
    // Encode unknown fields that have an ordinal that should appear before this field.
    while (next_unknown != _unknown_data.end() && next_unknown->first < {{ .Ordinal }}) {
      size_t envelope_base = base + (next_unknown->first - 1) * envelope_size;
    {{- if $.IsResourceType }}
      ::fidl::EncodeUnknownData(_encoder, &next_unknown->second, envelope_base);
    {{- else }}
      ::fidl::EncodeUnknownBytes(_encoder, &next_unknown->second, envelope_base);
    {{- end }}
      std::advance(next_unknown, 1);
    }

    const size_t length_before = _encoder->CurrentLength();
    const size_t handles_before = _encoder->CurrentHandleCount();

    size_t envelope_base = base + ({{ .Ordinal }} - 1) * envelope_size;
    switch (_encoder->wire_format()) {
      case ::fidl::internal::WireFormatVersion::kV1: {
        ::fidl::Encode(
          _encoder,
          &{{ .FieldDataName }}.value,
          _encoder->Alloc(::fidl::EncodingInlineSize<{{ .Type }}, ::fidl::Encoder>(_encoder))
      {{- if .HandleInformation -}}
          , ::fidl::HandleInformation{
            .object_type = {{ .HandleInformation.ObjectType }},
            .rights = {{ .HandleInformation.Rights }}
          }
      {{- end -}}
          );

        {{/* Call GetPtr after Encode because the buffer may move. */ -}}
        fidl_envelope_t* envelope = _encoder->GetPtr<fidl_envelope_t>(envelope_base);
        envelope->num_bytes = static_cast<uint32_t>(_encoder->CurrentLength() - length_before);
        envelope->num_handles = static_cast<uint32_t>(_encoder->CurrentHandleCount() - handles_before);
        envelope->presence = FIDL_ALLOC_PRESENT;
        break;
      }
      case ::fidl::internal::WireFormatVersion::kV2: {
        if (::fidl::EncodingInlineSize<{{ .Type }}>(_encoder) <= FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
          ::fidl::Encode(_encoder, &{{ .FieldDataName }}.value, envelope_base
          {{- if .HandleInformation -}}
              , ::fidl::HandleInformation{
                .object_type = {{ .HandleInformation.ObjectType }},
                .rights = {{ .HandleInformation.Rights }}
              }
          {{- end -}});

          {{/* Call GetPtr after Encode because the buffer may move. */ -}}
          fidl_envelope_v2_t* envelope = _encoder->GetPtr<fidl_envelope_v2_t>(envelope_base);
          envelope->num_handles = static_cast<uint16_t>(_encoder->CurrentHandleCount() - handles_before);
          envelope->flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
          break;
        }

        ::fidl::Encode(
          _encoder,
          &{{ .FieldDataName }}.value,
          _encoder->Alloc(::fidl::EncodingInlineSize<{{ .Type }}, ::fidl::Encoder>(_encoder))
      {{- if .HandleInformation -}}
          , ::fidl::HandleInformation{
            .object_type = {{ .HandleInformation.ObjectType }},
            .rights = {{ .HandleInformation.Rights }}
          }
      {{- end -}}
          );

        {{/* Call GetPtr after Encode because the buffer may move. */ -}}
        fidl_envelope_v2_t* envelope = _encoder->GetPtr<fidl_envelope_v2_t>(envelope_base);
        envelope->num_bytes = static_cast<uint32_t>(_encoder->CurrentLength() - length_before);
        envelope->num_handles = static_cast<uint16_t>(_encoder->CurrentHandleCount() - handles_before);
        envelope->flags = 0;
        break;
      }
    }
  }
  {{- end }}
  // Encode any remaining unknown fields (i.e. ones that have an ordinal outside
  // the range of known ordinals)
  for (auto curr = next_unknown; curr != _unknown_data.end(); ++curr) {
    size_t envelope_base = base + (curr->first - 1) * envelope_size;
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
    size_t envelope_base = base + ({{ .Ordinal }} - 1) * sizeof(fidl_envelope_v2_t);
    fidl_envelope_v2_t* envelope = _decoder->GetPtr<fidl_envelope_v2_t>(envelope_base);
    if (*reinterpret_cast<const void* const*>(envelope) != nullptr) {
      ::fidl::Decode(_decoder, _value->mutable_{{ .Name }}(),
        _decoder->EnvelopeValueOffset(envelope));
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

      size_t envelope_base = base + (ordinal - 1) * sizeof(fidl_envelope_v2_t);
      fidl_envelope_v2_t* envelope = _decoder->GetPtr<fidl_envelope_v2_t>(envelope_base);
      if (*reinterpret_cast<const void* const*>(envelope) != nullptr) {
        auto unknown_info = _decoder->EnvelopeUnknownDataInfo(envelope);
        auto result = _value->_unknown_data.emplace(std::piecewise_construct, std::forward_as_tuple(ordinal), std::forward_as_tuple());
        auto iter = result.first;
    {{- if .IsResourceType }}
        iter->second.bytes.resize(unknown_info.num_bytes);
        iter->second.handles.resize(unknown_info.num_handles);
        ::fidl::DecodeUnknownDataContents(_decoder, &iter->second, unknown_info.value_offset);
    {{- else }}
        iter->second.resize(unknown_info.num_bytes);
        ::fidl::DecodeUnknownBytesContents(_decoder, &iter->second, unknown_info.value_offset);
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
{{- EndifFuchsia -}}
{{ end }}

{{- end }}

{{- define "TableTraits" }}
{{- if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
template <>
struct CodingTraits<{{ . }}>
    : public EncodableCodingTraits<{{ . }}, 16, 16> {};

inline zx_status_t Clone(const {{ . }}& _value,
                         {{ . }}* result) {
  return _value.Clone(result);
}
template<>
struct Equality<{{ . }}> {
  bool operator()(const {{ . }}& _lhs, const {{ . }}& _rhs) const {
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
{{- EndifFuchsia -}}
{{ end }}

{{- end }}
`
