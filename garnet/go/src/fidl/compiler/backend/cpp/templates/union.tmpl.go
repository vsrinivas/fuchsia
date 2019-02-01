// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "UnionDeclaration" }}
{{range .DocComments}}
//{{ . }}
{{- end}}
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

  bool has_invalid_tag() const { return Which() == Tag::Invalid; }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return Which() == Tag({{ $index }}); }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Type.Decl }}& {{ .Name }}() {
    if (!is_{{ .Name }}()) {
      value_.emplace<{{ $index }} + 1>();
    }
    return value_.template get<{{ $index }} + 1>();
  }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  const {{ .Type.Decl }}& {{ .Name }}() const { return value_.template get<{{ $index }} + 1>(); }
  void set_{{ .Name }}({{ .Type.Decl }} value);
  {{- end }}

  Tag Which() const {
    size_t index = value_.index();
    if (index == 0) {
      return Tag::Invalid;
    } else {
      return Tag(index - 1);
    }
  }

 private:
  friend bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs);

  using Variant = fit::internal::variant<fit::internal::monostate
  {{- range .Members -}}
    , {{ .Type.Decl -}}
  {{- end -}}
  >;
  Variant value_;
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

{{- define "UnionDefinition" }}
{{ .Name }}::{{ .Name }}() : value_() {}

{{ .Name }}::~{{ .Name }}() {
}

{{ .Name }}::{{ .Name }}({{ .Name }}&& other) : value_(std::move(other.value_)) {
}

{{ .Name }}& {{ .Name }}::operator=({{ .Name }}&& other) {
  if (this != &other) {
    value_ = std::move(other.value_);
  }
  return *this;
}

void {{ .Name }}::Encode(::fidl::Encoder* encoder, size_t offset) {
  fidl_union_tag_t tag = static_cast<fidl_union_tag_t>(Which());
  ::fidl::Encode(encoder, &tag, offset);
  switch (tag) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    ::fidl::Encode(encoder, &{{ .Name }}(), offset + {{ .Offset }});
    break;
  {{- end }}
   default:
    break;
  }
}

void {{ .Name }}::Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset) {
  fidl_union_tag_t tag;
  ::fidl::Decode(decoder, &tag, offset);
  switch (tag) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    {
      {{ .Type.Decl }} member{};
      ::fidl::Decode(decoder, &member, offset + {{ .Offset }});
      value->set_{{ .Name }}(std::move(member));
      break;
    }
  {{- end }}
   default:
    value->value_.emplace<0>();
  }
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  zx_status_t status = ZX_OK;
  switch (Which()) {
    {{- range $index, $member := .Members }}
    case Tag::{{ .TagName }}:
      {
        {{ .Type.Decl }} member{};
        status = ::fidl::Clone({{ .Name }}(), &member);
        if (status == ZX_OK) {
	  result->set_{{ .Name }}(std::move(member));
        }
      }
      break;
    {{- end }}
    case Tag::Invalid:
      result->value_.emplace<0>();
      break;
  }
  return status;
}

bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  if (lhs.Which() != rhs.Which()) {
    return false;
  }
  switch (lhs.Which()) {
    {{- range $index, $member := .Members }}
    case {{ $.Name }}::Tag::{{ .TagName }}:
      return ::fidl::Equals(lhs.{{ .Name }}(), rhs.{{ .Name }}());
    {{- end }}
    case {{ .Name }}::Tag::Invalid:
      return true;
    default:
      return false;
  }
}

{{- range $index, $member := .Members }}

void {{ $.Name }}::set_{{ .Name }}({{ .Type.Decl }} value) {
  value_.emplace<static_cast<size_t>(Tag::{{ .TagName }}) + 1>(std::move(value));
}

{{- end }}

{{- end }}

{{- define "UnionTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .Size }}> {};

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}
{{- end }}
`
