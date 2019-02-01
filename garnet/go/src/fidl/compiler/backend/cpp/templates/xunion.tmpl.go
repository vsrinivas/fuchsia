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
    {{ .TagName }} = {{ .Ordinal }},
  {{- end }}
  };

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* encoder, size_t offset);
  static void Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;

  bool empty() const { return tag_ == Tag::Empty; }

  {{- range .Members }}

  bool is_{{ .Name }}() const { return tag_ == {{ .Ordinal }}; }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Type.Decl }}& {{ .Name }}() {
    EnsureStorageInitialized({{ .Ordinal }});
    return {{ .StorageName }};
  }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  const {{ .Type.Decl }}& {{ .Name }}() const { return {{ .StorageName }}; }
  void set_{{ .Name }}({{ .Type.Decl }} value);
  {{- end }}

  Tag Which() const { return Tag(tag_); }

 private:
  friend bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs);
  void Destroy();
  void EnsureStorageInitialized(::fidl_xunion_tag_t tag);

  ::fidl_xunion_tag_t tag_ = Tag::Empty;
  union {
  {{- range .Members }}
    {{ .Type.Decl }} {{ .StorageName }};
  {{- end }}
  };
};

bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs);
inline bool operator!=(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  return !(lhs == rhs);
}

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
   case {{ .Ordinal }}:
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
     case {{ .Ordinal }}:
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
    case {{ .Ordinal }}: {
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

  const size_t envelope_offset = decoder->GetOffset(xunion->envelope.data);

  switch (value->tag_) {
  {{- range .Members }}
   case {{ .Ordinal }}:
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
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  result->Destroy();
  result->tag_ = tag_;
  switch (tag_) {
    {{- range .Members }}
    case {{ .Ordinal }}:
      {{- if .Type.Dtor }}
      new (&result->{{ .StorageName }}) {{ .Type.Decl }}();
      {{- end }}
      return ::fidl::Clone({{ .StorageName }}, &result->{{ .StorageName }});
    {{- end }}
    default:
      return ZX_OK;
  }
}

bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  if (lhs.tag_ != rhs.tag_) {
    return false;
  }
  switch (lhs.tag_) {
    {{- range .Members }}
    case {{ .Ordinal }}:
      return ::fidl::Equals(lhs.{{ .StorageName }}, rhs.{{ .StorageName }});
    {{- end }}
    case {{ .Name }}::Tag::Empty:
      return true;
    default:
      return false;
  }
}

{{- range $member := .Members }}

void {{ $.Name }}::set_{{ .Name }}({{ .Type.Decl }} value) {
  EnsureStorageInitialized({{ .Ordinal }});
  {{ .StorageName }} = std::move(value);
}

{{- end }}

void {{ .Name }}::Destroy() {
  switch (tag_) {
  {{- range .Members }}
   case {{ .Ordinal }}:
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
      case {{ .Ordinal }}:
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

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}
{{- end }}
`
