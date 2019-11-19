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
class {{ .Name }} final {
 public:
  {{ .Name }}();
  ~{{ .Name }}();

  {{ .Name }}({{ .Name }}&&);
  {{ .Name }}& operator=({{ .Name }}&&);

  {{- if .Result }}
  {{ .Name }}(fit::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>&& result) {
    ZX_ASSERT(!result.is_pending());
    if (result.is_ok()) {
      {{- if eq 0 .Result.ValueArity }}
      set_response({{ .Result.ValueStructDecl }}{});
      {{- else if eq 1 .Result.ValueArity }}
      set_response({{ .Result.ValueStructDecl }}{result.take_value()});
      {{- else }}
      set_response(result.take_value());
      {{- end }}
    } else {
      set_err(std::move(result.take_error()));
    }
  }

  {{ .Name }}(fit::ok_result<{{ .Result.ValueDecl }}>&& result)
    : {{ .Name }}(fit::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>(std::move(result))) { }

  {{ .Name }}(fit::error_result<{{ .Result.ErrorDecl }}>&& result)
    : {{ .Name }}(fit::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>(std::move(result))) { }

  operator fit::result<{{ .Result.ValueDecl }}, {{ .Result.ErrorDecl }}>() && {
    if (is_err()) {
      return fit::error(err());
    }
    {{- if eq 0 .Result.ValueArity }}
    return fit::ok();
    {{- else if eq 1 .Result.ValueArity }}
    {{ .Result.ValueTupleDecl }} value_tuple = std::move(response());
    return fit::ok(std::move(std::get<0>(value_tuple)));
    {{- else }}
    return fit::ok(std::move(response()));
    {{- end }}
  }
  {{- end }}

  enum class Tag : fidl_union_tag_t {
  {{- range $index, $member := .Members }}
    {{ .TagName }} = {{ $index }},
  {{- end }}
    Invalid = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  };

  enum __attribute__((enum_extensibility(closed))) XUnionTag : fidl_xunion_tag_t {
  {{- range .Members }}
    {{ .TagName }} = {{ .XUnionOrdinal }},  // {{ .XUnionOrdinal | printf "%#x" }}
  {{- end }}
  };

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* _encoder, size_t _offset);
  void EncodeAsXUnionBytes(::fidl::Encoder* _encoder, size_t _offset);
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
  XUnionTag WhichXUnionTag() const {
    switch (Which()) {
      {{- range .Members }}
    case Tag::{{ .TagName }}: return XUnionTag::{{ .TagName }};
      {{- end }}
    case Tag::Invalid:{{/* This is not ideal but we should never hit this case. */}} return XUnionTag(0);
    }
  }

  using Variant = fit::internal::variant<fit::internal::monostate
  {{- range .Members -}}
    , {{ .Type.Decl -}}
  {{- end -}}
  >;
  Variant value_;
};

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
  if (_encoder->ShouldEncodeUnionAsXUnion()) {
    EncodeAsXUnionBytes(_encoder, _offset);
    return;
  }

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

void {{ .Name }}::EncodeAsXUnionBytes(::fidl::Encoder* _encoder, size_t _offset) {
  const size_t length_before = _encoder->CurrentLength();
  const size_t handles_before = _encoder->CurrentHandleCount();

  size_t envelope_offset = 0;

  switch (Which()) {
    {{- range .Members }}
    case Tag::{{ .TagName }}: {
      envelope_offset = _encoder->Alloc(::fidl::EncodingInlineSize<{{ .Type.Identifier }}, ::fidl::Encoder>(_encoder));
      ::fidl::Encode(_encoder, &{{ .Name }}(), envelope_offset);
      break;
    }
    {{- end }}
    default:
       break;
  }

  {{/* Note that _encoder->GetPtr() must be called after every call to
       _encoder->Alloc(), since _encoder.bytes_ could be re-sized and moved.
     */ -}}

  fidl_xunion_t* xunion = _encoder->GetPtr<fidl_xunion_t>(_offset);
  assert(xunion->envelope.presence == FIDL_ALLOC_ABSENT);

  if (envelope_offset) {
    xunion->tag = WhichXUnionTag();
    xunion->envelope.num_bytes = _encoder->CurrentLength() - length_before;
    xunion->envelope.num_handles = _encoder->CurrentHandleCount() - handles_before;
    xunion->envelope.presence = FIDL_ALLOC_PRESENT;
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

{{- range $index, $member := .Members }}

void {{ $.Name }}::set_{{ .Name }}({{ .Type.Decl }} value) {
  value_.emplace<static_cast<size_t>(Tag::{{ .TagName }}) + 1>(std::move(value));
}

{{- end }}

{{- end }}

{{- define "UnionTraits" }}
template <>
struct IsFidlUnion<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};

template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .InlineSizeOld }}, {{ .InlineSizeV1NoEE }}> {};

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
