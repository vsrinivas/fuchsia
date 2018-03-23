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
  };

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* encoder, size_t offset);
  static void Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;
  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return tag_ == {{ $index }}; }
  {{ .Type.Decl }}& {{ .Name }}() { return {{ .StorageName }}; }
  const {{ .Type.Decl }}& {{ .Name }}() const { return {{ .StorageName }}; }
  void set_{{ .Name }}({{ .Type.Decl }} value) {
    Destroy();
    tag_ = {{ $index }};
    {{ .StorageName }} = std::move(value);
  }
  {{- end }}

  Tag Which() const { return Tag(tag_); }

 private:
  void Destroy();

  ::fidl_union_tag_t tag_;
  union {
  {{- range .Members }}
    {{ .Type.Decl }} {{ .StorageName }};
  {{- end }}
  };
};

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
    {{ .StorageName }} = std::move(other.{{ .StorageName }});
    break;
  {{- end }}
   default:
    break;
  }
}

{{ .Name }}& {{ .Name }}::operator=({{ .Name }}&& other) {
  if (this != &other) {
    tag_ = other.tag_;
    switch (tag_) {
    {{- range $index, $member := .Members }}
     case {{ $index }}:
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
  ::fidl::Decode(decoder, &value->tag_, offset);
  switch (value->tag_) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    ::fidl::Decode(decoder, &value->{{ .StorageName }}, offset + {{ .Offset }});
    break;
  {{- end }}
   default:
    break;
  }
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  result->tag_ = tag_;
  switch (tag_) {
    {{- range $index, $member := .Members }}
     case {{ $index }}:
      return ::fidl::Clone({{ .StorageName }}, &result->{{ .StorageName }});
    {{- end }}
     default:
      return ZX_ERR_INVALID_ARGS;
  }
}

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
