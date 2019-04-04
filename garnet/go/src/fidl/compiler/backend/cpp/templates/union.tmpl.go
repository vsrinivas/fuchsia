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

  void Encode(::fidl::Encoder* _encoder, size_t _offset);
  static void Decode(::fidl::Decoder* _decoder, {{ .Name }}* _value, size_t _offset);
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
#ifdef FIDL_OPERATOR_EQUALS
  friend bool operator==(const {{ .Name }}& _lhs, const {{ .Name }}& _rhs);
#endif

  using Variant = fit::internal::variant<fit::internal::monostate
  {{- range .Members -}}
    , {{ .Type.Decl -}}
  {{- end -}}
  >;
  Variant value_;
};

#ifdef FIDL_OPERATOR_EQUALS
bool operator==(const {{ .Name }}& _lhs, const {{ .Name }}& _rhs);
inline bool operator!=(const {{ .Name }}& _lhs, const {{ .Name }}& _rhs) {
  return !(_lhs == _rhs);
}
#endif

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

void {{ .Name }}::Encode(::fidl::Encoder* _encoder, size_t _offset) {
  fidl_union_tag_t _tag = static_cast<fidl_union_tag_t>(Which());
  ::fidl::Encode(_encoder, &_tag, _offset);
  switch (_tag) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    ::fidl::Encode(_encoder, &{{ .Name }}(), _offset + {{ .Offset }});
    break;
  {{- end }}
   default:
    break;
  }
}

void {{ .Name }}::Decode(::fidl::Decoder* _decoder, {{ .Name }}* _value, size_t _offset) {
  fidl_union_tag_t _tag;
  ::fidl::Decode(_decoder, &_tag, _offset);
  switch (_tag) {
  {{- range $index, $member := .Members }}
   case {{ $index }}:
    {
      {{ .Type.Decl }} _member{};
      ::fidl::Decode(_decoder, &_member, _offset + {{ .Offset }});
      _value->set_{{ .Name }}(std::move(_member));
      break;
    }
  {{- end }}
   default:
    _value->value_.emplace<0>();
  }
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* _result) const {
  zx_status_t _status = ZX_OK;
  switch (Which()) {
    {{- range $index, $member := .Members }}
    case Tag::{{ .TagName }}:
      {
        {{ .Type.Decl }} _member{};
        _status = ::fidl::Clone({{ .Name }}(), &_member);
        if (_status == ZX_OK) {
          _result->set_{{ .Name }}(std::move(_member));
        }
      }
      break;
    {{- end }}
    case Tag::Invalid:
      _result->value_.emplace<0>();
      break;
  }
  return _status;
}

#ifdef FIDL_OPERATOR_EQUALS
bool operator==(const {{ .Name }}& _lhs, const {{ .Name }}& _rhs) {
  if (_lhs.Which() != _rhs.Which()) {
    return false;
  }
  switch (_lhs.Which()) {
    {{- range $index, $member := .Members }}
    case {{ $.Name }}::Tag::{{ .TagName }}:
      return ::fidl::Equals(_lhs.{{ .Name }}(), _rhs.{{ .Name }}());
    {{- end }}
    case {{ .Name }}::Tag::Invalid:
      return true;
    default:
      return false;
  }
}
#endif

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

template<>
struct Equality<{{ .Namespace }}::{{ .Name }}> {
  static inline bool Equals(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) {
    if (_lhs.Which() != _rhs.Which()) {
      return false;
    }
    switch (_lhs.Which()) {
      {{- range $index, $member := .Members }}
      case {{ $.Namespace}}::{{ $.Name }}::Tag::{{ .TagName }}:
	return ::fidl::Equals(_lhs.{{ .Name }}(), _rhs.{{ .Name }}());
      {{- end }}
      case {{ .Namespace }}::{{ .Name }}::Tag::Invalid:
	return true;
      default:
	return false;
    }
  }
};

{{- end }}
`
