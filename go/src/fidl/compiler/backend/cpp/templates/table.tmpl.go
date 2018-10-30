// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Table = `
{{- define "TableForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "TableDeclaration" }}
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Name }}  {
 public:
  static const fidl_type_t* FidlType;
  {{- range .Members }}
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  const {{ .Type.Decl }}* {{ .Name }}() const {
    return {{ .FieldPresenceName }} ? &{{ .FieldDataName }}.value : nullptr;
  }
  bool {{ .MethodHasName }}() const {
    return {{ .FieldPresenceName }};
  }
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Type.Decl }}* mutable_{{ .Name }}() {
    if (!{{ .FieldPresenceName }}) {
      {{ .FieldPresenceName }} = true;
      Construct(&{{ .FieldDataName }}.value);
    }
    return &{{ .FieldDataName }}.value;
  }
  void set_{{ .Name }}({{ .Type.Decl }} value) {
    if (!{{ .FieldPresenceName }}) {
      {{ .FieldPresenceName }} = true;
      Construct(&{{ .FieldDataName }}.value, std::move(value));
    } else {
      {{ .FieldDataName }}.value = std::move(value);
    }
  }
  void {{ .MethodClearName }}() {
    if (!{{ .FieldPresenceName }}) {
      return;
    }
    {{ .FieldPresenceName }} = false;
    Destruct(&{{ .FieldDataName }}.value);
  }
  {{- end }}

  {{ .Name }}();
  {{ .Name }}({{ .Name }}&& other);
  ~{{ .Name }}();
  {{ .Name }}& operator=({{ .Name }}&& other);

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* encoder, size_t offset);
  static void Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;

 private:
  template <class T, class... Args>
  void Construct(T* p, Args&&... args) {
    new (p) T(std::forward<Args>(args)...);
  }

  template <class T>
  void Destruct(T* p) {
    p->~T();
  }

  {{- range .Members }}
  bool {{ .FieldPresenceName }} : 1;
  {{- end }}

  {{- range .Members }}
  union {{ .ValueUnionName }} {
    {{ .ValueUnionName }}() {}
    ~{{ .ValueUnionName }}() {}

    {{ .Type.Decl }} value;
  };
  {{ .ValueUnionName }} {{ .FieldDataName }};
  {{- end }}
};

bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs);
inline bool operator!=(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  return !(lhs == rhs);
}

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- end }}

{{- define "TableDefinition" }}
extern "C" const fidl_type_t {{ .TableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .TableType }};

{{ .Name }}::{{ .Name }}() :
{{- range $index, $element := .Members }}
  {{if $index}},{{end}}
  has_{{ $element.Name }}_(false)
{{- end }} {
}

{{ .Name }}::{{ .Name }}({{ .Name }}&& other) {
  {{- range .Members }}
  {{ .FieldPresenceName }} = other.{{ .FieldPresenceName }};
  if ({{ .FieldPresenceName }}) {
    Construct(&{{ .FieldDataName }}.value, std::move(other.{{ .FieldDataName }}.value));
  }
  {{- end }}
}

{{ .Name }}::~{{ .Name }}() {
  {{- range .Members }}
  if ({{ .FieldPresenceName }}) {
    Destruct(&{{ .FieldDataName }}.value);
  }
  {{- end }}
}

{{ .Name }}& {{ .Name }}::operator=({{ .Name }}&& other) {
  {{- range .Members }}
  if (other.{{ .FieldPresenceName }}) {
    if ({{ .FieldPresenceName }}) {
      {{ .FieldDataName }}.value = std::move(other.{{ .FieldDataName }}.value);
    } else {
      {{ .FieldPresenceName }} = true;
      Construct(&{{ .FieldDataName }}.value, std::move(other.{{ .FieldDataName }}.value));
    }
  } else if ({{ .FieldPresenceName }}) {
    {{ .FieldPresenceName }} = false;
    Destruct(&{{ .FieldDataName }}.value);
  }
  {{- end }}
  return *this;
}

void {{ .Name }}::Encode(::fidl::Encoder* encoder, size_t offset) {
  ::fidl::EncodeVectorPointer(encoder, {{ .BiggestOrdinal }}, offset);
  size_t base = encoder->Alloc({{ .BiggestOrdinal }} * sizeof(uint64_t));
  {{- range .Members }}
  if ({{ .FieldPresenceName }}) {
    const size_t length_before = encoder->CurrentLength();
    const size_t handles_before = encoder->CurrentHandleCount();
    ::fidl::Encode(
        encoder,
        &{{ .FieldDataName }}.value,
        encoder->Alloc(CodingTraits<{{ .Type.Decl }}>::encoded_size));
    uint64_t presence =
        ((encoder->CurrentLength() - length_before) << 32) |
        (encoder->CurrentHandleCount() - handles_before);
    ::fidl::Encode(encoder, &presence, base + ({{ .Ordinal }} - 1) * sizeof(uint64_t));
  }
  {{- end }}
}

void {{ .Name }}::Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset) {
  fidl_vector_t* encoded = decoder->GetPtr<fidl_vector_t>(offset);
  size_t base;
  size_t count;
  if (!encoded->data) {
    goto clear_all;
  }

  base = decoder->GetOffset(encoded->data);
  count = encoded->count;

  {{- range .Members }}
  if (count >= {{ .Ordinal }}) {
    uint64_t presence;
    ::fidl::Decode(decoder, &presence, base + ({{ .Ordinal }} - 1) * sizeof(uint64_t));
    if (presence != 0) {
      ::fidl::Decode(decoder, value->mutable_{{ .Name }}(), decoder->GetOffset(presence));
    } else {
      value->{{ .MethodClearName }}();
    }
  } else {
    goto done_{{ .Ordinal }};
  }
  {{- end }}

  return;

  // Clear unset values.
clear_all:
  {{- range .Members }}
done_{{ .Ordinal }}:
  value->{{ .MethodClearName }}();
  {{- end }}
  return;
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  {{- range .Members }}
  if ({{ .FieldPresenceName }}) {
    zx_status_t _status = ::fidl::Clone({{ .FieldDataName }}.value, result->mutable_{{ .Name }}());
    if (_status != ZX_OK)
      return _status;
  } else {
    result->{{ .MethodClearName }}();
  }
  {{- end }}
  return ZX_OK;
}

bool operator==(const {{ .Name }}& lhs, const {{ .Name }}& rhs) {
  {{- range .Members }}
  if (lhs.{{ .MethodHasName }}()) {
    if (!rhs.{{ .MethodHasName }}()) {
      return false;
    }
    if (!::fidl::Equals(*lhs.{{ .Name }}(), *rhs.{{ .Name }}())) {
      return false;
    }
  } else if (rhs.{{ .MethodHasName }}()) {
    return false;
  }
  {{- end }}
  return true;
}
{{- end }}

{{- define "TableTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .Size }}> {};

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return value.Clone(result);
}
{{- end }}
`
