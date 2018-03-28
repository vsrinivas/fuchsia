// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "UnionDeclaration" }}
class {{ .Name }} {
 public:
  {{ .Name }}();
  ~{{ .Name }}();

  {{ .Name }}({{ .Name }}&&);
  {{ .Name }}& operator=({{ .Name }}&&);

  enum class Tag : fidl_union_tag_t {
  {{- range $index, $member := .Members }}
    {{ .TagName }} = {{ $index }},
  {{- end }}
    Invalid = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  };

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* encoder, size_t offset);
  static void Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;

  bool has_invalid_tag() const { return tag_ == ::std::numeric_limits<::fidl_union_tag_t>::max(); }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return tag_ == {{ $index }}; }
  {{ .Type.Decl }}& {{ .Name }}() { return {{ .StorageName }}; }
  const {{ .Type.Decl }}& {{ .Name }}() const { return {{ .StorageName }}; }
  void set_{{ .Name }}({{ .Type.Decl }} value);
  {{- end }}

  Tag Which() const { return Tag(tag_); }

 private:
  friend bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs);
  void Destroy();

  ::fidl_union_tag_t tag_;
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

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- end }}

{{- define "UnionDefinition" }}
{{ .Name }}::{{ .Name }}() : tag_(::std::numeric_limits<::fidl_union_tag_t>::max()) {}

{{ .Name }}::~{{ .Name }}() {
  Destroy();
}

{{ .Name }}::{{ .Name }}({{ .Name }}&& other) : tag_(other.tag_) {
  switch (tag_) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
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
    {{- range $index, $member := .Members }}
     case {{ $index }}:
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
  ::fidl::Encode(encoder, &tag_, offset);
  switch (tag_) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    ::fidl::Encode(encoder, &{{ .StorageName }}, offset + {{ .Offset }});
    break;
  {{- end }}
   default:
    break;
  }
}

void {{ .Name }}::Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset) {
  value->Destroy();
  ::fidl::Decode(decoder, &value->tag_, offset);
  switch (value->tag_) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    {{- if .Type.Dtor }}
    new (&value->{{ .StorageName }}) {{ .Type.Decl }}();
    {{- end }}
    ::fidl::Decode(decoder, &value->{{ .StorageName }}, offset + {{ .Offset }});
    break;
  {{- end }}
   default:
    break;
  }
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  result->Destroy();
  result->tag_ = tag_;
  switch (tag_) {
    {{- range $index, $member := .Members }}
     case {{ $index }}:
      {{- if .Type.Dtor }}
      new (&result->{{ .StorageName }}) {{ .Type.Decl }}();
      {{- end }}
      return ::fidl::Clone({{ .StorageName }}, &result->{{ .StorageName }});
    {{- end }}
     default:
      return ZX_ERR_INVALID_ARGS;
  }
}

bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  if (lhs.tag_ != rhs.tag_) {
    return false;
  }
  switch (lhs.tag_) {
    {{- range $index, $member := .Members }}
     case {{ $index }}:
      return lhs.{{ .StorageName }} == rhs.{{ .StorageName }};
    {{- end }}
     default:
      return false;
  }
}

{{- range $index, $member := .Members }}

void {{ $.Name }}::set_{{ .Name }}({{ .Type.Decl }} value) {
  Destroy();
  tag_ = {{ $index }};
  {{- if .Type.Dtor }}
  new (&{{ .StorageName }}) {{ .Type.Decl }}();
  {{- end }}
  {{ .StorageName }} = std::move(value);
}

{{- end }}

void {{ .Name }}::Destroy() {
  switch (tag_) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    {{- if .Type.Dtor }}
    {{ .StorageName }}.{{ .Type.Dtor }}();
    {{- end }}
    break;
  {{- end }}
   default:
    break;
  }
  tag_ = ::std::numeric_limits<::fidl_union_tag_t>::max();
}
{{- end }}

{{- define "UnionTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .Size }}> {};

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return value.Clone(result);
}
{{- end }}
`
