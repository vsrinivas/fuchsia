// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const XUnion = `
{{- define "XUnionForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "XUnionDeclaration" }}
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Name }} {
 public:
 static const fidl_type_t* FidlType;

  {{ .Name }}();
  ~{{ .Name }}();

  {{ .Name }}({{ .Name }}&&);
  {{ .Name }}& operator=({{ .Name }}&&);

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
      * UnknownData() will return nullptr.
    * if the xunion is non-strict (flexible) and has been de-serialized from a xunion with an
      ordinal that's unknown to the client's schema:
      * tag_ will be the raw ordinal from the serialized xunion,
      * Ordinal() will return tag_,
      * Which() will return Tag::kUnknown.
      * UnknownData() will return a pointer to a valid std::vector<uint8_t> with the unknown data.

    */ -}}

    enum : fidl_xunion_tag_t {
      kEmpty = kFidlXUnionEmptyTag,
    };

    enum __attribute__((enum_extensibility(closed))) Tag : fidl_xunion_tag_t {
    {{ if .IsFlexible -}}
      kUnknown = 0,
      {{- /* TODO(FIDL-728): Remove the Empty tag below. */}}
      Empty = kUnknown,  // DEPRECATED: use kUnknown instead.
    {{ end -}}
    {{- range .Members }}
    {{ .TagName }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  };

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::overnet::internal::Encoder* encoder, size_t offset);
  static void Decode(::overnet::internal::Decoder* decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;

  {{- range .Members }}

  bool is_{{ .Name }}() const { return tag_ == Tag::{{ .TagName }}; }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Type.OvernetEmbeddedDecl }}& {{ .Name }}() {
    EnsureStorageInitialized(Tag::{{ .TagName }});
    return {{ .StorageName }};
  }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  const {{ .Type.OvernetEmbeddedDecl }}& {{ .Name }}() const { return {{ .StorageName }}; }
  {{ $.Name }}& set_{{ .Name }}({{ .Type.OvernetEmbeddedDecl }} value);
  {{- end }}

  Tag Which() const {
    {{ if .IsFlexible }}
    switch (tag_) {
      {{- range .Members }}
    case Tag::{{ .TagName }}:
      {{- end }}
      return Tag(tag_);
    default:
      return Tag::kUnknown;
    }
    {{ else }}
    return Tag(tag_);
    {{ end }}
  }

  // You probably want to use Which() method instead of Ordinal(). Use Ordinal() only when you need
  // access to the raw integral ordinal value.
  fidl_xunion_tag_t Ordinal() const {
    return tag_;
  }

  const std::vector<uint8_t>* UnknownData() const {
    {{- if .IsStrict }}
    return nullptr;
    {{- else }}
    if (Which() != Tag::kUnknown) {
      return nullptr;
    }

    return &unknown_data_;
    {{- end }}
  }

  friend ::fidl::Equality<{{ .Namespace }}::embedded::{{ .Name }}>;

 private:
  void Destroy();
  void EnsureStorageInitialized(::fidl_xunion_tag_t tag);

  ::fidl_xunion_tag_t tag_ = kEmpty;
  union {
  {{- range .Members }}
    {{ .Type.OvernetEmbeddedDecl }} {{ .StorageName }};
  {{- end }}
  {{- if .IsFlexible }}
    std::vector<uint8_t> unknown_data_;
  {{- end }}
  };
};

inline zx_status_t Clone(const {{ .Namespace }}::embedded::{{ .Name }}& value,
                         {{ .Namespace }}::embedded::{{ .Name }}* result) {
  return value.Clone(result);
}

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- end }}

{{- define "XUnionDefinition" }}
extern "C" const fidl_type_t {{ .TableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .TableType }};

{{ .Name }}::{{ .Name }}() {}

{{ .Name }}::~{{ .Name }}() {
  Destroy();
}

{{ .Name }}::{{ .Name }}({{ .Name }}&& other) : tag_(other.tag_) {
  switch (tag_) {
  {{- range .Members }}
   case Tag::{{ .TagName }}:
    {{- if .Type.OvernetEmbeddedDtor }}
    new (&{{ .StorageName }}) {{ .Type.OvernetEmbeddedDecl }}();
    {{- end }}
    {{ .StorageName }} = std::move(other.{{ .StorageName }});
    break;
  {{- end }}
    case kEmpty:
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
      case Tag::{{ .TagName }}:
        {{- if .Type.OvernetEmbeddedDtor }}
        new (&{{ .StorageName }}) {{ .Type.OvernetEmbeddedDecl }}();
        {{- end }}
        {{ .StorageName }} = std::move(other.{{ .StorageName }});
        break;
      {{- end }}
      case kEmpty:
        break;
      {{- if .IsFlexible }}
      default:
        new (&unknown_data_) decltype(unknown_data_);
        unknown_data_ = std::move(other.unknown_data_);
        break;
      {{- end }}
    }
  }
  return *this;
}

void {{ .Name }}::Encode(::overnet::internal::Encoder* encoder, size_t offset) {
  const size_t length_before = encoder->CurrentLength();
  const size_t handles_before = encoder->CurrentHandleCount();

  size_t envelope_offset = 0;

  switch (tag_) {
    {{- range .Members }}
    case Tag::{{ .TagName }}: {
      envelope_offset = encoder->Alloc(::fidl::CodingTraits<{{ .Type.OvernetEmbeddedDecl }}>::encoded_size);
      ::fidl::Encode(encoder, &{{ .StorageName }}, envelope_offset);
      break;
    }
    {{- end }}
    {{- if .IsFlexible }}
    case Tag::kUnknown:
    {{- end }}
    default:
       break;
  }

  {{/* Note that encoder->GetPtr() must be called after every call to
       encoder->Alloc(), since encoder.bytes_ could be re-sized and moved.
     */ -}}

  fidl_xunion_t* xunion = encoder->GetPtr<fidl_xunion_t>(offset);
  assert(xunion->envelope.presence == FIDL_ALLOC_ABSENT);

  if (envelope_offset) {
    xunion->tag = tag_;
    xunion->envelope.num_bytes = encoder->CurrentLength() - length_before;
    xunion->envelope.num_handles = encoder->CurrentHandleCount() - handles_before;
    xunion->envelope.presence = FIDL_ALLOC_PRESENT;
  }
}

void {{ .Name }}::Decode(::overnet::internal::Decoder* decoder, {{ .Name }}* value, size_t offset) {
  fidl_xunion_t* xunion = decoder->GetPtr<fidl_xunion_t>(offset);

  if (!xunion->envelope.data) {
    value->EnsureStorageInitialized(kEmpty);
    return;
  }

  value->EnsureStorageInitialized(xunion->tag);

{{ if len .Members }}
  const size_t envelope_offset = decoder->GetOffset(xunion->envelope.data);

  switch (value->tag_) {
  {{- range .Members }}
   case Tag::{{ .TagName }}:
    {{- if .Type.OvernetEmbeddedDtor }}
    new (&value->{{ .StorageName }}) {{ .Type.OvernetEmbeddedDecl }}();
    {{- end }}
    ::fidl::Decode(decoder, &value->{{ .StorageName }}, envelope_offset);
    break;
  {{- end }}
  {{ if .IsFlexible -}}
   default:
    value->unknown_data_.resize(xunion->envelope.num_bytes);
    memcpy(value->unknown_data_.data(), xunion->envelope.data, xunion->envelope.num_bytes);
    break;
  {{ end -}}
  }
{{ end }}
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  result->Destroy();
  result->tag_ = tag_;
  switch (tag_) {
    {{- range .Members }}
    case Tag::{{ .TagName }}:
      {{- if .Type.OvernetEmbeddedDtor }}
      new (&result->{{ .StorageName }}) {{ .Type.OvernetEmbeddedDecl }}();
      {{- end }}
      return ::fidl::Clone({{ .StorageName }}, &result->{{ .StorageName }});
    {{- end }}
    default:
      return ZX_OK;
  }
}

{{- range $member := .Members }}

{{ $.Name }}& {{ $.Name }}::set_{{ .Name }}({{ .Type.OvernetEmbeddedDecl }} value) {
  EnsureStorageInitialized(Tag::{{ .TagName }});
  {{ .StorageName }} = std::move(value);
  return *this;
}

{{- end }}

void {{ .Name }}::Destroy() {
  switch (tag_) {
    {{- range .Members }}
    case Tag::{{ .TagName }}:
      {{- if .Type.OvernetEmbeddedDtor }}
      {{ .StorageName }}.{{ .Type.OvernetEmbeddedDtor }}();
      {{- end }}
      break;
    {{- end }}
    {{ if .IsFlexible }}
    case kEmpty:
      break;
    default:
      unknown_data_.~vector();
      break;
    {{ else }}
    default:
      break;
    {{ end }}
  }
  tag_ = kEmpty;
}

void {{ .Name }}::EnsureStorageInitialized(::fidl_xunion_tag_t tag) {
  if (tag_ != tag) {
    Destroy();
    tag_ = tag;
    switch (tag_) {
      case kEmpty:
        break;
      {{- range .Members }}
      {{- if .Type.OvernetEmbeddedDtor }}
      case Tag::{{ .TagName }}:
        new (&{{ .StorageName }}) {{ .Type.OvernetEmbeddedDecl }}();
        break;
      {{- end }}
      {{- end }}
      default:
        {{- if .IsFlexible }}
        new (&unknown_data_) decltype(unknown_data_);
        {{- end }}
        break;
    }
  }
}

{{- end }}

{{- define "XUnionTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::embedded::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::embedded::{{ .Name }}, {{ .Size }}> {};

template <>
struct CodingTraits<std::unique_ptr<{{ .Namespace }}::embedded::{{ .Name }}>> {
  static constexpr size_t encoded_size = {{ .Size }};

  static void Encode(::overnet::internal::Encoder* encoder, std::unique_ptr<{{ .Namespace }}::embedded::{{ .Name }}>* value, size_t offset) {
    {{/* TODO(FIDL-481): Disallow empty xunions (but permit nullable/optional
         xunions). */ -}}

    auto&& p_xunion = *value;
    if (p_xunion) {
      p_xunion->Encode(encoder, offset);
    }
  }

  static void Decode(::overnet::internal::Decoder* decoder, std::unique_ptr<{{ .Namespace }}::embedded::{{ .Name }}>* value, size_t offset) {
    fidl_xunion_t* encoded = decoder->GetPtr<fidl_xunion_t>(offset);
    if (encoded->tag == 0) {
      value->reset(nullptr);
      return;
    }

    value->reset(new {{ .Namespace }}::embedded::{{ .Name }});

    {{ .Namespace }}::embedded::{{ .Name }}::Decode(decoder, value->get(), offset);
  }
};

inline zx_status_t Clone(const {{ .Namespace }}::embedded::{{ .Name }}& value,
                         {{ .Namespace }}::embedded::{{ .Name }}* result) {
  return {{ .Namespace }}::embedded::Clone(value, result);
}

template <>
struct ToEmbeddedTraits<{{ .Namespace }}::{{ .Name }}> {
  static {{ .Namespace }}::embedded::{{ .Name }} Lift(const {{ .Namespace }}::{{ .Name }}& _value) {
    {{ .Namespace }}::embedded::{{ .Name }} _out;
    switch (_value.Which()) {
      {{- range $index, $member := .Members }}
      case {{ $.Namespace }}::{{ $.Name }}::Tag::{{ $member.TagName }}:
        _out.set_{{ $member.Name }}(ToEmbedded(_value.{{ $member.Name }}()));
        break;
      {{- end }}
      {{ if .IsFlexible }}
      case {{ $.Namespace }}::{{ $.Name }}::Tag::kUnknown:
        break;
      {{- end }}
    }
    return _out;
  }
};

template<>
struct Equality<{{ .Namespace }}::embedded::{{ .Name }}> {
  static inline bool Equals(const {{ .Namespace }}::embedded::{{ .Name }}& _lhs, const {{ .Namespace }}::embedded::{{ .Name }}& _rhs) {
    if (_lhs.Ordinal() != _rhs.Ordinal()) {
      return false;
    }

    {{ with $xunion := . -}}
    switch (_lhs.Ordinal()) {
      case {{ $xunion.Namespace }}::{{ $xunion.Name }}::kEmpty:
        return true;
      {{- range .Members }}
      case {{ $xunion.Namespace }}::embedded::{{ $xunion.Name }}::Tag::{{ .TagName }}:
        return ::fidl::Equals(_lhs.{{ .StorageName }}, _rhs.{{ .StorageName }});
      {{- end }}
      {{ if .IsFlexible -}}
      default:
        return *_lhs.UnknownData() == *_rhs.UnknownData();
      {{ else }}
      default:
        return false;
      {{ end -}}
    }
    {{end -}}
  }
};

{{- end }}
`
