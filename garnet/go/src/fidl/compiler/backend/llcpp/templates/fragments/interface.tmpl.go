// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Interface = `
{{- define "InterfaceForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "RequestCodingTable" -}}
{{- if .LLProps.EncodeRequest -}}
&{{ .RequestTypeName }}
{{- else -}}
nullptr
{{- end }}
{{- end }}

{{- define "ResponseCodingTable" -}}
{{- if .LLProps.DecodeResponse -}}
&{{ .ResponseTypeName }}
{{- else -}}
nullptr
{{- end }}
{{- end }}

{{- define "InterfaceDeclaration" }}
{{ "" }}
  {{- range .Methods }}
    {{- if .LLProps.EncodeRequest }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
    {{- end }}
    {{- if .LLProps.DecodeResponse }}
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
  {{- /* End trailing line feed after encoding tables. */}}

{{- range .DocComments }}
//{{ . }}
{{- end }}
class {{ .Name }} final {
 public:
{{ "" }}
  {{- range .Methods }}

    {{- if .HasResponse }}
      {{- if .Response }}
  struct {{ .Name }}Response {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .Response }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
        {{- end }}

    static constexpr const fidl_type_t* Type = {{ template "ResponseCodingTable" . }};
    static constexpr uint32_t MaxNumHandles = {{ .ResponseMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .ResponseSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .ResponseMaxOutOfLine }};
  };
      {{- else }}
  using {{ .Name }}Response = ::fidl::AnyZeroArgMessage;
      {{- end }}
    {{- end }}

    {{- if .HasRequest }}
      {{- if .Request }}
  struct {{ .Name }}Request {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .Request }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
        {{- end }}

    static constexpr const fidl_type_t* Type = {{ template "RequestCodingTable" . }};
    static constexpr uint32_t MaxNumHandles = {{ .RequestMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .RequestSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .RequestMaxOutOfLine }};

        {{- if and .HasResponse .Response }}
    using ResponseType = {{ .Name }}Response;
        {{- end }}
  };
{{ "" }}
      {{- else }}
  using {{ .Name }}Request = ::fidl::AnyZeroArgMessage;
{{ "" }}
      {{- end }}
    {{- end }}

  {{- end }}

  class SyncClient final {
   public:
    SyncClient(::zx::channel channel) : channel_(std::move(channel)) {}

    ~SyncClient() {}
{{ "" }}
    {{- range .Methods }}
      {{- /* Client-calling functions do not apply to events. */}}
      {{- if not .HasRequest -}} {{ continue }} {{- end -}}
      {{- if .LLProps.CBindingCompatible }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    zx_status_t {{ template "SyncRequestCFlavorMethodSignature" . }};
      {{- end }}
{{ "" }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Caller provides the backing storage for FIDL message via request and response buffers.
    zx_status_t {{ template "SyncRequestCallerAllocateMethodSignature" . }};
{{ "" }}
      {{- end }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Messages are encoded and decoded in-place.
    {{ if .Response }}::fidl::DecodeResult<{{ .Name }}Response>{{ else }}zx_status_t{{ end }} {{ template "SyncRequestInPlaceMethodSignature" . }};
{{ "" }}
      {{- end }}
    {{- end }}
   private:
    ::zx::channel channel_;
  };

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

{{- define "FillRequestStructMembers" }}
{{- range $param := . }}
  _request.{{ $param.Name }} = std::move({{ $param.Name }});
{{- end }}
{{- end }}

{{- define "ReturnResponseStructMembers" }}
{{- range $param := . }}
  *out_{{ $param.Name }} = std::move(_response.{{ $param.Name }});
{{- end }}
{{- end }}

{{- define "InterfaceDefinition" }}

namespace {
{{ $interface := . -}}

{{- range .Methods }}
[[maybe_unused]]
constexpr uint32_t {{ .OrdinalName }} = {{ .Ordinal }}u;
  {{- if .LLProps.EncodeRequest }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
  {{- end }}
  {{- if .LLProps.DecodeResponse }}
extern "C" const fidl_type_t {{ .ResponseTypeName }};
  {{- end }}
{{- end }}

}  // namespace

{{- range .Methods }}
  {{- /* Client-calling functions do not apply to events. */}}
  {{- if not .HasRequest -}} {{ continue }} {{- end }}
  {{- if .LLProps.CBindingCompatible }}
{{ "" }}
    {{- template "SyncRequestCFlavorMethodDefinition" . }}
  {{- end }}
  {{- if or .Request .Response }}
{{ "" }}
    {{- template "SyncRequestCallerAllocateMethodDefinition" . }}
  {{- end }}
  {{- if or .Request .Response }}
{{ "" }}
    {{- template "SyncRequestInPlaceMethodDefinition" . }}
  {{- end }}
{{ "" }}
{{- end }}

{{- end }}
`
