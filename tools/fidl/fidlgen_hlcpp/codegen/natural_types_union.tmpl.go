// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const unionTemplate = `
{{- define "UnionForwardDeclaration" }}
{{ EnsureNamespace . }}
class {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "UnionDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
{{- .Docs }}
class {{ .Name }} final {
 public:
 static const fidl_type_t* FidlType;

  {{ .Name }}();
  ~{{ .Name }}();

  {{ .Name }}({{ .Name }}&&);
  {{ .Name }}& operator=({{ .Name }}&&);

  {{ range .Members }}
  static {{ $.Name }} With{{ .UpperCamelCaseName }}({{ .Type }}&&);
  {{- end }}

  {{/* There are two different tag types here:

    * fidl_xunion_tag_t: This is an "open" enum that encompasses all possible ordinal values
      (including zero). Ordinal() returns a fidl_xunion_tag_t.
    * An inner ::Tag enum: This only contains valid ordinals for this xunion. Which() returns a
      ::Tag.

    The two types generally carry the same value. However:

    * If the ordinal is zero, which is only ever the case when the xunion is first constructed and
      not yet set:
      * tag_, which is a fidl_xunion_tag_t, will be kEmpty (which is kFidlXUnionEmptyTag, or 0).
        kEmpty is intended for internal use only; clients should use ::Tag instead.
      * Ordinal() will return kEmpty.
      * Which() will return Tag::kUnknown.
    * if the xunion is non-strict (flexible) and has been de-serialized from a xunion with an
      ordinal that's unknown to the client's schema:
      * tag_ will be the raw ordinal from the serialized xunion,
      * Ordinal() will return tag_,
      * Which() will return Tag::kUnknown.
      * UnknownBytes() will return a pointer to a valid std::vector<uint8_t> with the unknown bytes.
      * if the xunion is a resource type:
        * UnknownHandles() will return a pointer to a valid std::vector<zx::handle> with the unknown handles.
    */ -}}

  enum __attribute__((enum_extensibility(closed))) {{ .TagEnum.Self }} : fidl_xunion_tag_t {
  {{ if .IsFlexible -}}
    {{ .TagUnknown.Self }} = 0,
    {{- /* TODO(fxbug.dev/8050): Remove the Empty tag below. */}}
    Empty = {{ .TagUnknown.Self }},  // DEPRECATED: use kUnknown instead.
  {{ end -}}
  {{- range .Members }}
    {{ .TagName.Self }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
    Invalid = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  };

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* encoder, size_t offset,
              cpp17::optional<::fidl::HandleInformation> maybe_handle_info = cpp17::nullopt);
  static void Decode(::fidl::Decoder* _decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;

  bool has_invalid_tag() const {
    return tag_ == Invalid;
  }

  {{- range .Members }}

  bool is_{{ .Name }}() const { return tag_ == {{ .TagName }}; }
  {{ .Docs }}
  {{ .Type }}& {{ .Name }}() {
    EnsureStorageInitialized({{ .TagName }});
    return {{ .StorageName }};
  }
  {{ .Docs }}
  const {{ .Type }}& {{ .Name }}() const {
    ZX_ASSERT(is_{{ .Name }}());
    return {{ .StorageName }};
  }
  {{ $.Name }}& set_{{ .Name }}({{ .Type }} value);
  {{- end }}

  {{- if .IsFlexible }}
  {{ .Name }}& SetUnknownData(fidl_xunion_tag_t ordinal, std::vector<uint8_t> bytes{{ if .IsResourceType }}, std::vector<zx::handle> handles{{ end }});
  {{- end }}

  {{ .TagEnum }} Which() const {
    {{ if .IsFlexible }}
    switch (tag_) {
      case {{ .TagInvalid }}:
      {{- range .Members }}
      case {{ .TagName }}:
      {{- end }}
        return {{ .TagEnum }}(tag_);
      default:
        return {{ .TagUnknown }};
    }
    {{ else }}
    return {{ .TagEnum }}(tag_);
    {{ end }}
  }

  // You probably want to use Which() method instead of Ordinal(). Use Ordinal() only when you need
  // access to the raw integral ordinal value.
  fidl_xunion_tag_t Ordinal() const {
    return tag_;
  }

{{- if .IsFlexible }}
  const std::vector<uint8_t>* UnknownBytes() const {
    if (Which() != {{ .TagUnknown }}) {
      return nullptr;
    }
  {{- if .IsResourceType }}
    return &unknown_data_.bytes;
  {{- else }}
    return &unknown_data_;
  {{- end }}
  }

  {{- if .IsResourceType }}
  const std::vector<zx::handle>* UnknownHandles() const { 
    if (Which() != {{ .TagUnknown }}) {
      return nullptr;
    }
    return &unknown_data_.handles;
  }
  {{- end }}
{{- end }}

  friend ::fidl::Equality<{{ . }}>;

  {{- if .Result }}
  {{ .Name }}(fpromise::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>&& result) {
    ZX_ASSERT(!result.is_pending());
    if (result.is_ok()) {
      {{- if eq 0 .Result.ValueArity }}
      set_response({{ .Result.ValueStructDecl }}{});
      {{- else }}
      set_response({{ .Result.ValueStructDecl }}{result.take_value()});
      {{- end }}
    } else {
      set_err(std::move(result.take_error()));
    }
  }
  {{ .Name }}(fpromise::ok_result<{{ .Result.ValueDecl }}>&& result)
    : {{ .Name }}(fpromise::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>(std::move(result))) { }
  {{ .Name }}(fpromise::error_result<{{ .Result.ErrorDecl }}>&& result)
    : {{ .Name }}(fpromise::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>(std::move(result))) { }
  operator fpromise::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>() && {
    if (is_err()) {
      return fpromise::error(err());
    }
    {{- if eq 0 .Result.ValueArity }}
    return fpromise::ok();
    {{- else if eq 1 .Result.ValueArity }}
    {{ .Result.ValueTupleDecl }} value_tuple = std::move(response());
    return fpromise::ok(std::move(std::get<0>(value_tuple)));
    {{- else }}
    return fpromise::ok(std::move(response()));
    {{- end }}
  }
  {{- end }}

 private:
  void Destroy();
  void EnsureStorageInitialized(::fidl_xunion_tag_t tag);

  ::fidl_xunion_tag_t tag_ = static_cast<fidl_xunion_tag_t>({{ .TagInvalid }});
  union {
  {{- range .Members }}
    {{ .Type }} {{ .StorageName }};
  {{- end }}
  {{- if .IsFlexible }}
    {{ if .IsResourceType }}::fidl::UnknownData{{ else }}std::vector<uint8_t>{{ end }} unknown_data_;
  {{- end }}
  };
};

inline zx_status_t Clone(const {{ . }}& value,
                         {{ . }}* result) {
  return value.Clone(result);
}

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{ end }}

{{- end }}

{{- define "UnionDefinition" }}
{{ EnsureNamespace . }}
{{- if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
extern "C" const fidl_type_t {{ .CodingTableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .CodingTableType }};

{{ .Name }}::{{ .Name }}() {}

{{ .Name }}::~{{ .Name }}() {
  Destroy();
}

{{ .Name }}::{{ .Name }}({{ .Name }}&& other) : tag_(other.tag_) {
  switch (tag_) {
  {{- range .Members }}
    case {{ .TagName }}:
    {{- if .Type.NeedsDtor }}
      new (&{{ .StorageName }}) {{ .Type }}();
    {{- end }}
      {{ .StorageName }} = std::move(other.{{ .StorageName }});
      break;
  {{- end }}
    case static_cast<fidl_xunion_tag_t>({{ .TagInvalid }}):
      break;
  {{- if .IsFlexible }}
    default:
      new (&unknown_data_) decltype(unknown_data_);
      unknown_data_ = std::move(other.unknown_data_);
      break;
  {{- end }}
  }
}

{{ .Name }}& {{ .Name }}::operator=({{ .Name }}&& other) {
  if (this != &other) {
    Destroy();
    tag_ = other.tag_;
    switch (tag_) {
    {{- range .Members }}
      case {{ .TagName }}:
        {{- if .Type.NeedsDtor }}
        new (&{{ .StorageName }}) {{ .Type }}();
        {{- end }}
        {{ .StorageName }} = std::move(other.{{ .StorageName }});
        break;
    {{- end }}
      case static_cast<fidl_xunion_tag_t>({{ .TagInvalid }}):
        break;
    {{- if .IsFlexible }}
      default:
        new (&unknown_data_) decltype(unknown_data_);
        unknown_data_= std::move(other.unknown_data_);
        break;
    {{- end }}
    }
  }
  return *this;
}

{{ range .Members -}}
{{ $.Name }} {{ $.Name }}::With{{ .UpperCamelCaseName }}({{ .Type }}&& val) {
  {{ $.Name }} result;
  result.set_{{ .Name }}(std::move(val));
  return result;
}
{{ end }}

void {{ .Name }}::Encode(::fidl::Encoder* encoder, size_t offset,
                         cpp17::optional<::fidl::HandleInformation> maybe_handle_info) {
  const size_t length_before = encoder->CurrentLength();
  const size_t handles_before = encoder->CurrentHandleCount();

  switch (Which()) {
    {{- range .Members }}
    case {{ .TagName }}: {
      switch (encoder->wire_format()) {
        case ::fidl::Encoder::WireFormat::V1: {
          ::fidl::Encode(
            encoder,
            &{{ .StorageName }},
            encoder->Alloc(::fidl::EncodingInlineSize<{{ .Type }}, ::fidl::Encoder>(encoder))
        {{- if .HandleInformation -}}
            , ::fidl::HandleInformation{
              .object_type = {{ .HandleInformation.ObjectType }},
              .rights = {{ .HandleInformation.Rights }}
            }
        {{- end -}}
          );

          {{/* Call GetPtr after Encode because the buffer may move. */ -}}
          fidl_xunion_t* xunion = encoder->GetPtr<fidl_xunion_t>(offset);
          xunion->tag = tag_;
          xunion->envelope.num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
          xunion->envelope.num_handles = static_cast<uint32_t>(encoder->CurrentHandleCount() - handles_before);
          xunion->envelope.presence = FIDL_ALLOC_PRESENT;
          break;
        }
        case ::fidl::Encoder::WireFormat::V2: {
          if (::fidl::EncodingInlineSize<{{ .Type }}>(encoder) <= FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
            ::fidl::Encode(encoder, &{{ .StorageName }}, offset + offsetof(fidl_xunion_v2_t, envelope)
            {{- if .HandleInformation -}}
                , ::fidl::HandleInformation{
                  .object_type = {{ .HandleInformation.ObjectType }},
                  .rights = {{ .HandleInformation.Rights }}
                }
            {{- end -}});

            {{/* Call GetPtr after Encode because the buffer may move. */ -}}
            fidl_xunion_v2_t* xunion = encoder->GetPtr<fidl_xunion_v2_t>(offset);
            xunion->tag = tag_;
            xunion->envelope.num_handles = static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
            xunion->envelope.flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
            break;
          }

          ::fidl::Encode(
            encoder,
            &{{ .StorageName }},
            encoder->Alloc(::fidl::EncodingInlineSize<{{ .Type }}, ::fidl::Encoder>(encoder))
        {{- if .HandleInformation -}}
            , ::fidl::HandleInformation{
              .object_type = {{ .HandleInformation.ObjectType }},
              .rights = {{ .HandleInformation.Rights }}
            }
        {{- end -}}
          );

          fidl_xunion_v2_t* xunion = encoder->GetPtr<fidl_xunion_v2_t>(offset);
          xunion->tag = tag_;
          xunion->envelope.num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
          xunion->envelope.num_handles = static_cast<uint16_t>(encoder->CurrentHandleCount() - handles_before);
          xunion->envelope.flags = 0;
          break;
        }
      }
      break;
    }
    {{- end }}
    {{- if .IsFlexible }}
    case {{ .TagUnknown }}: {
      {{- if .IsResourceType }}
      ::fidl::EncodeUnknownData(encoder, &unknown_data_, offset + offsetof(fidl_xunion_t, envelope));
      {{- else }}
      ::fidl::EncodeUnknownBytes(encoder, &unknown_data_, offset + offsetof(fidl_xunion_t, envelope));
      {{- end }}
      *encoder->GetPtr<uint64_t>(offset) = tag_;
      break;
    }
    {{- end }}
    default:
       break;
  }
}

void {{ .Name }}::Decode(::fidl::Decoder* _decoder, {{ .Name }}* value, size_t offset) {
  fidl_xunion_t* xunion = _decoder->GetPtr<fidl_xunion_t>(offset);

  if (!xunion->envelope.data) {
    value->EnsureStorageInitialized(static_cast<fidl_xunion_tag_t>({{ .TagInvalid }}));
    return;
  }

  value->EnsureStorageInitialized(xunion->tag);

{{ if len .Members }}
  const size_t envelope_offset = _decoder->GetOffset(xunion->envelope.data);

  switch (value->tag_) {
  {{- range .Members }}
    case {{ .TagName }}:
      {{- if .Type.NeedsDtor }}
      new (&value->{{ .StorageName }}) {{ .Type }}();
      {{- end }}
      ::fidl::Decode(_decoder, &value->{{ .StorageName }}, envelope_offset);
      break;
  {{- end }}
    default:
  {{ if .IsFlexible -}}
    {{- if .IsResourceType }}
      value->unknown_data_.bytes.resize(xunion->envelope.num_bytes);
      value->unknown_data_.handles.resize(xunion->envelope.num_handles);
      ::fidl::DecodeUnknownDataContents(_decoder, &value->unknown_data_, envelope_offset);
    {{- else }}
      value->unknown_data_.resize(xunion->envelope.num_bytes);
      ::fidl::DecodeUnknownBytesContents(_decoder, &value->unknown_data_, envelope_offset);
    {{- end }}
  {{ end -}}
      break;
  }
{{ end }}
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  result->Destroy();
  result->tag_ = tag_;
  switch (tag_) {
    case {{ .TagInvalid }}:
      return ZX_OK;
    {{- range .Members }}
    case {{ .TagName }}:
      {{- if .Type.NeedsDtor }}
      new (&result->{{ .StorageName }}) {{ .Type }}();
      {{- end }}
      return ::fidl::Clone({{ .StorageName }}, &result->{{ .StorageName }});
    {{- end }}
    default:
    {{- if .IsFlexible }}
      new (&result->unknown_data_) decltype(unknown_data_);
      return ::fidl::Clone(unknown_data_, &result->unknown_data_);
    {{ end -}}
      return ZX_OK;
  }
}

{{- range $member := .Members }}

{{ $.Name }}& {{ $.Name }}::set_{{ .Name }}({{ .Type }} value) {
  EnsureStorageInitialized({{ .TagName }});
  {{ .StorageName }} = std::move(value);
  return *this;
}

{{- end }}

{{- if .IsFlexible }}
{{ .Name }}& {{ .Name }}::SetUnknownData(fidl_xunion_tag_t ordinal, std::vector<uint8_t> bytes{{ if .IsResourceType }}, std::vector<zx::handle> handles{{ end }}) {
  EnsureStorageInitialized(ordinal);
  {{- if .IsResourceType }}
  unknown_data_.bytes = std::move(bytes);
  unknown_data_.handles = std::move(handles);
  {{- else }}
  unknown_data_ = std::move(bytes);
  {{- end }}
  return *this;
}
{{- end }}

void {{ .Name }}::Destroy() {
  switch (tag_) {
  {{- range .Members }}
    case {{ .TagName }}:
      {{- if .Type.NeedsDtor }}
      {{ .StorageName }}.~decltype({{ .StorageName }})();
      {{- end }}
      break;
  {{- end }}
  {{ if .IsFlexible }}
    case static_cast<fidl_xunion_tag_t>({{ .TagInvalid }}):
      break;
    default:
      unknown_data_.~decltype(unknown_data_)();
      break;
  {{ else }}
    default:
      break;
  {{ end }}
  }
  tag_ = static_cast<fidl_xunion_tag_t>({{ .TagInvalid }});
}

void {{ .Name }}::EnsureStorageInitialized(::fidl_xunion_tag_t tag) {
  if (tag_ != tag) {
    Destroy();
    tag_ = tag;
    switch (tag_) {
      case static_cast<fidl_xunion_tag_t>({{ .TagInvalid }}):
        break;
      {{- range .Members }}
      case {{ .TagName }}:
        new (&{{ .StorageName }}) {{ .Type }}();
        break;
      {{- end }}
      default:
      {{- if .IsFlexible }}
        new (&unknown_data_) decltype(unknown_data_);
      {{- end }}
        break;
    }
  }
}
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{ end }}

{{- end }}

{{- define "UnionTraits" }}
{{- if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
template <>
struct IsFidlXUnion<{{ . }}> : public std::true_type {};

template <>
struct CodingTraits<{{ . }}>
    : public EncodableCodingTraits<{{ . }}, 24, 16> {};

template <>
struct CodingTraits<std::unique_ptr<{{ . }}>> {
  static constexpr size_t inline_size_v1_no_ee = 24;
  static constexpr size_t inline_size_v2 = 16;

  static void Encode(Encoder* encoder, std::unique_ptr<{{ . }}>* value, size_t offset,
                     cpp17::optional<::fidl::HandleInformation> maybe_handle_info) {
    {{/* TODO(fxbug.dev/7805): Disallow empty xunions (but permit nullable/optional
         xunions). */ -}}

    auto&& p_xunion = *value;
    if (p_xunion) {
      p_xunion->Encode(encoder, offset);
    }
  }

  static void Decode(Decoder* _decoder, std::unique_ptr<{{ . }}>* value, size_t offset) {
    fidl_xunion_t* encoded = _decoder->GetPtr<fidl_xunion_t>(offset);
    if (encoded->tag == 0) {
      value->reset(nullptr);
      return;
    }

    value->reset(new {{ . }});

    {{ . }}::Decode(_decoder, value->get(), offset);
  }
};

inline zx_status_t Clone(const {{ . }}& value,
                         {{ . }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}

template<>
struct Equality<{{ . }}> {
  bool operator()(const {{ . }}& _lhs, const {{ . }}& _rhs) const {
    if (_lhs.Ordinal() != _rhs.Ordinal()) {
      return false;
    }

    {{ with $xunion := . -}}
    switch (_lhs.Ordinal()) {
      case static_cast<fidl_xunion_tag_t>({{ .TagInvalid }}):
        return true;
    {{- range .Members }}
      case {{ .TagName }}:
        return ::fidl::Equals(_lhs.{{ .StorageName }}, _rhs.{{ .StorageName }});
      {{- end }}
      {{ if .IsFlexible -}}
      default:
        return ::fidl::Equals(_lhs.unknown_data_, _rhs.unknown_data_);
      {{ else }}
      default:
        return false;
      {{ end -}}
      }
    {{ end -}}
  }
};
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{ end }}

{{- end }}
`
