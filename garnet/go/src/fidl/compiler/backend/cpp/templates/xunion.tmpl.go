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

  enum Tag : fidl_xunion_tag_t {
    Empty = 0,
  {{- range .Members }}
    {{ .TagName }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  };

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* encoder, size_t offset);
  static void Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;

  {{- range .Members }}

  bool is_{{ .Name }}() const { return tag_ == Tag::{{ .TagName }}; }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Type.Decl }}& {{ .Name }}() {
    EnsureStorageInitialized(Tag::{{ .TagName }});
    return {{ .StorageName }};
  }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  const {{ .Type.Decl }}& {{ .Name }}() const { return {{ .StorageName }}; }
  void set_{{ .Name }}({{ .Type.Decl }} value);
  {{- end }}

  Tag Which() const { return Tag(tag_); }

  friend ::fidl::Equality<{{ .Namespace }}::{{ .Name }}>;

 private:
#ifdef FIDL_OPERATOR_EQUALS
  friend bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs);
#endif
  void Destroy();
  void EnsureStorageInitialized(::fidl_xunion_tag_t tag);

  ::fidl_xunion_tag_t tag_ = Tag::Empty;
  union {
  {{- range .Members }}
    {{ .Type.Decl }} {{ .StorageName }};
  {{- end }}
  };
};

#ifdef FIDL_OPERATOR_EQUALS
bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs);
inline bool operator!=(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  return !(lhs == rhs);
}
#endif

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
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
    {{- if .Type.Dtor }}
    new (&{{ .StorageName }}) {{ .Type.Decl }}();
    {{- end }}
    {{ .StorageName }} = std::move(other.{{ .StorageName }});
    break;
  {{- end }}
   default:
    break;
  }
}

{{ .Name }}& {{ .Name }}::operator=({{ .Name }}&& other) {
  if (this != &other) {
    Destroy();
    tag_ = other.tag_;
    switch (tag_) {
    {{- range .Members }}
     case Tag::{{ .TagName }}:
      {{- if .Type.Dtor }}
      new (&{{ .StorageName }}) {{ .Type.Decl }}();
      {{- end }}
      {{ .StorageName }} = std::move(other.{{ .StorageName }});
      break;
    {{- end }}
     default:
      break;
    }
  }
  return *this;
}

void {{ .Name }}::Encode(::fidl::Encoder* encoder, size_t offset) {
  const size_t length_before = encoder->CurrentLength();
  const size_t handles_before = encoder->CurrentHandleCount();

  size_t envelope_offset = 0;

  switch (tag_) {
    {{- range .Members }}
    case Tag::{{ .TagName }}: {
      envelope_offset = encoder->Alloc(::fidl::CodingTraits<{{ .Type.Decl }}>::encoded_size);
      ::fidl::Encode(encoder, &{{ .StorageName }}, envelope_offset);
      break;
    }
    {{- end }}
    case Tag::Empty:
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

void {{ .Name }}::Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset) {
  fidl_xunion_t* xunion = decoder->GetPtr<fidl_xunion_t>(offset);

  if (!xunion->envelope.data) {
    value->EnsureStorageInitialized(Tag::Empty);
    return;
  }

  value->EnsureStorageInitialized(xunion->tag);

{{ if len .Members }}
  const size_t envelope_offset = decoder->GetOffset(xunion->envelope.data);

  switch (value->tag_) {
  {{- range .Members }}
   case Tag::{{ .TagName }}:
    {{- if .Type.Dtor }}
    new (&value->{{ .StorageName }}) {{ .Type.Decl }}();
    {{- end }}
    ::fidl::Decode(decoder, &value->{{ .StorageName }}, envelope_offset);
    break;
  {{- end }}
   default:
    {{/* The decoder doesn't have a schema for this tag, so it simply does
         nothing. The generated code doesn't need to update the offsets to
         "skip" the secondary object nor claim handles, since BufferWalker does
         that. */ -}}
    break;
  }
{{ end }}
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  result->Destroy();
  result->tag_ = tag_;
  switch (tag_) {
    {{- range .Members }}
    case Tag::{{ .TagName }}:
      {{- if .Type.Dtor }}
      new (&result->{{ .StorageName }}) {{ .Type.Decl }}();
      {{- end }}
      return ::fidl::Clone({{ .StorageName }}, &result->{{ .StorageName }});
    {{- end }}
    default:
      return ZX_OK;
  }
}

#ifdef FIDL_OPERATOR_EQUALS
bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  if (lhs.tag_ != rhs.tag_) {
    return false;
  }

  {{ with $xunion := . -}}
  switch (lhs.tag_) {
    {{- range .Members }}
    case {{ $xunion.Name }}::Tag::{{ .TagName }}:
      return ::fidl::Equals(lhs.{{ .StorageName }}, rhs.{{ .StorageName }});
    {{- end }}
    case {{ $xunion.Name }}::Tag::Empty:
      return true;
    default:
      return false;
  }
  {{end -}}
}
#endif

{{- range $member := .Members }}

void {{ $.Name }}::set_{{ .Name }}({{ .Type.Decl }} value) {
  EnsureStorageInitialized(Tag::{{ .TagName }});
  {{ .StorageName }} = std::move(value);
}

{{- end }}

void {{ .Name }}::Destroy() {
  switch (tag_) {
  {{- range .Members }}
   case Tag::{{ .TagName }}:
    {{- if .Type.Dtor }}
    {{ .StorageName }}.{{ .Type.Dtor }}();
    {{- end }}
    break;
  {{- end }}
   default:
    break;
  }
  tag_ = Tag::Empty;
}

void {{ .Name }}::EnsureStorageInitialized(::fidl_xunion_tag_t tag) {
  if (tag_ != tag) {
    Destroy();
    tag_ = tag;
    switch (tag_) {
      {{- range .Members }}
      {{- if .Type.Dtor }}
      case Tag::{{ .TagName }}:
        new (&{{ .StorageName }}) {{ .Type.Decl }}();
        break;
      {{- end }}
      {{- end }}
      default:
        break;
    }
  }
}

{{- end }}

{{- define "XUnionTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .Size }}> {};

template <>
struct CodingTraits<std::unique_ptr<{{ .Namespace }}::{{ .Name }}>> {
  static constexpr size_t encoded_size = {{ .Size }};

  static void Encode(Encoder* encoder, std::unique_ptr<{{ .Namespace }}::{{ .Name }}>* value, size_t offset) {
    {{/* TODO(FIDL-481): Disallow empty xunions (but permit nullable/optional
         xunions). */ -}}

    auto&& p_xunion = *value;
    if (p_xunion) {
      p_xunion->Encode(encoder, offset);
    } else {
      {{/* |empty| is explicitly a non-static variable so that we don't use
           binary space, and sacrifice a little runtime overhead instead to
           construct the empty xunion on the stack. */ -}}
      {{ .Namespace }}::{{ .Name }} empty;
      empty.Encode(encoder, offset);
    }
  }

  static void Decode(Decoder* decoder, std::unique_ptr<{{ .Namespace }}::{{ .Name }}>* value, size_t offset) {
    value->reset(new {{ .Namespace }}::{{ .Name }});

    fidl_xunion_t* encoded = decoder->GetPtr<fidl_xunion_t>(offset);
    if (encoded->tag == 0) {
      return;
    }

    {{ .Namespace }}::{{ .Name }}::Decode(decoder, value->get(), offset);
  }
};

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}

template<>
struct Equality<{{ .Namespace }}::{{ .Name }}> {
  static inline bool Equals(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) {
    if (_lhs.Which() != _rhs.Which()) {
      return false;
    }

    {{ with $xunion := . -}}
    switch (_lhs.Which()) {
      {{- range .Members }}
      case {{ $xunion.Namespace}}::{{ $xunion.Name }}::Tag::{{ .TagName }}:
        return ::fidl::Equals(_lhs.{{ .StorageName }}, _rhs.{{ .StorageName }});
      {{- end }}
      case {{ $xunion.Namespace}}::{{ $xunion.Name }}::Tag::Empty:
        return true;
      default:
        return false;
    }
    {{end -}}
  }
};

{{- end }}
`
