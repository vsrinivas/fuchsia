// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.LLDecl }}& {{ $param.Name }}
  {{- end -}}
{{- end }}

{{- define "CallerBufferParams" -}}
{{- if . -}}
::fidl::BytePart& request_buffer, ::fidl::BytePart& response_buffer, {{ range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.LLDecl }}& {{ $param.Name }}
  {{- end -}}
{{- end -}}
{{- end }}

{{- define "OutParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.LLDecl }}* out_{{ $param.Name }}
  {{- end -}}
{{- end }}

{{- define "AsyncRequestMethodSignature" -}}

{{- end }}

{{- define "EventMethodSignature" -}}

{{- end }}

{{- define "SyncRequestMethodSignature" -}}
  {{- if .Response -}}
{{ .Name }}({{ template "Params" .Request }}{{ if .Request }}, {{ end }}{{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}({{ template "Params" .Request }})
  {{- end -}}
{{- end }}

{{- define "SyncCallerBufferRequestMethodSignature" -}}
  {{- if .Response -}}
{{ .Name }}({{ template "CallerBufferParams" .Request }}{{ if .Request }}, {{ end }}{{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}({{ template "CallerBufferParams" .Request }})
  {{- end -}}
{{- end }}

{{- define "SyncInPlaceRequestMethodSignature" -}}
{{ .Name }}({{ if .HasRequest }}::fidl::DecodedMessage<{{ .Name }}Request> params{{ if .HasResponse }}, {{ end }}{{ end }}{{ if .HasResponse }}::fidl::BytePart response_buffer{{ end }})
{{- end }}

{{- define "InterfaceDeclaration" }}
{{ "" }}
  {{- range .Methods }}
    {{- if and .HasRequest .Request }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
    {{- end }}
    {{- if and .HasResponse .Response }}
extern "C" const fidl_type_t {{ .ResponseTypeName }};
    {{- end }}
  {{- end }}
  {{- /* Trailing line feed after encoding tables. */}}
  {{- range .Methods }}
    {{- if and .HasRequest .Request -}}
{{ "" }}
{{ break }}
    {{- end }}
    {{- if and .HasResponse .Response -}}
{{ "" }}
{{ break }}
    {{- end }}
  {{- end }}
{{- range .DocComments }}
//{{ . }}
{{- end }}
class {{ .Name }} {
 public:
  {{ .Name }}();
  virtual ~{{ .Name }}();
{{ "" }}

  {{- range .Methods }}
    {{- if and .HasResponse .Response }}
  struct {{ .Name }}Response {
    FIDL_ALIGNDECL
    {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
    {{- range $index, $param := .Response }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
    {{- end }}

    static constexpr const fidl_type_t* Type = &{{ .ResponseTypeName }};
    static constexpr uint32_t MaxNumHandles = {{ .ResponseMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .ResponseSize }};
    [[maybe_unused]]
    static constexpr uint32_t MaxOutOfLine = {{ .ResponseMaxOutOfLine }};
  };
    {{- end }}
    {{- if and .HasRequest .Request }}
  struct {{ .Name }}Request {
    FIDL_ALIGNDECL
    {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
    {{- range $index, $param := .Request }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
    {{- end }}

    static constexpr const fidl_type_t* Type = &{{ .RequestTypeName }};
    static constexpr uint32_t MaxNumHandles = {{ .RequestMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .RequestSize }};
    [[maybe_unused]]
    static constexpr uint32_t MaxOutOfLine = {{ .RequestMaxOutOfLine }};

    {{- if and .HasResponse .Response }}
    using ResponseType = {{ .Name }}Response;
    {{- end }}
  };
{{ "" }}
    {{- end }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  zx_status_t {{ template "SyncRequestMethodSignature" . }};
{{ "" }}
    {{- if or .Request .Response }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  zx_status_t {{ template "SyncCallerBufferRequestMethodSignature" . }};
{{ "" }}
    {{- end }}
    {{- if or .Request .Response }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  {{ if .HasResponse }}::fidl::DecodeResult<{{ .Name }}Response>{{ else }}zx_status_t{{ end }} {{ template "SyncInPlaceRequestMethodSignature" . }};
{{ "" }}
    {{- end }}
  {{- end }}
};

{{- end }}

{{- define "InterfaceTraits" -}}
{{ $interface := . -}}
{{ range .Methods -}}
{{ $method := . -}}
{{- if and .HasRequest .Request }}

template <>
struct IsFidlType<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request> : public std::true_type {};
template <>
struct IsFidlMessage<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request> : public std::true_type {};
static_assert(sizeof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request)
    == {{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request::PrimarySize);
{{- range $index, $param := .Request }}
static_assert(offsetof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ $method.Name }}Request, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- if and .HasResponse .Response }}

template <>
struct IsFidlType<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response> : public std::true_type {};
template <>
struct IsFidlMessage<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response> : public std::true_type {};
static_assert(sizeof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response)
    == {{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response::PrimarySize);
{{- range $index, $param := .Response }}
static_assert(offsetof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ $method.Name }}Response, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- end }}
{{- end }}

{{- define "InterfaceDefinition" }}

namespace {
{{ $interface := . -}}

{{ range .Methods }}
[[maybe_unused]]
constexpr uint32_t {{ .OrdinalName }} = {{ .Ordinal }}u;
  {{- if .HasRequest }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
  {{- end }}
  {{- if .HasResponse }}
extern "C" const fidl_type_t {{ .ResponseTypeName }};
  {{- end }}
{{- end }}

}  // namespace

{{ .Name }}::{{ .Name }}() = default;
{{ .Name }}::~{{ .Name }}() = default;

{{- $interface := . }}

{{- range .Methods }}
{{ "" }}
zx_status_t {{ $interface.Name }}::{{ template "SyncRequestMethodSignature" . }} {
  return ZX_ERR_NOT_SUPPORTED;
}
  {{- if or .Request .Response }}
{{ "" }}
zx_status_t {{ $interface.Name }}::{{ template "SyncCallerBufferRequestMethodSignature" . }} {
  return ZX_ERR_NOT_SUPPORTED;
}
  {{- end }}
  {{- if or .Request .Response }}
{{ "" }}
{{ if .HasResponse }}::fidl::DecodeResult<{{ $interface.Name }}::{{ .Name }}Response>{{ else }}zx_status_t{{ end }} {{ $interface.Name }}::{{ template "SyncInPlaceRequestMethodSignature" . }} {
{{- if .HasResponse }}
  ::fidl::DecodeResult<{{ $interface.Name }}::{{ .Name }}Response> result;
  return result;
{{- else -}}
  return ZX_ERR_NOT_SUPPORTED;
{{- end }}
}
  {{- end }}
{{ "" }}
{{- end }}

{{- end }}
`
