// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentProtocolTmpl = `
{{- define "Protocol:ForwardDeclaration:Header" }}
  {{ EnsureNamespace . }}
  class {{ .Name }};
{{- end }}


{{- define "Method:ClientAllocationComment:Helper" -}}
  {{- if SyncCallTotalStackSizeV1 . -}}
    Allocates {{ SyncCallTotalStackSizeV1 . }} bytes of {{ "" -}}
    {{- if not .Request.ClientAllocationV1.IsStack -}}
      response 
    {{- else -}}
      {{- if not .Response.ClientAllocationV1.IsStack -}}
        request
      {{- else -}}
        message
      {{- end -}}
    {{- end }} buffer on the stack.
  {{- end }}
  {{- if and .Request.ClientAllocationV1.IsStack .Response.ClientAllocationV1.IsStack -}}
    {{ "" }} No heap allocation necessary.
  {{- else }}
    {{- if not .Request.ClientAllocationV1.IsStack }} Request is heap-allocated. {{- end }}
    {{- if not .Response.ClientAllocationV1.IsStack }} Response is heap-allocated. {{- end }}
  {{- end }}
{{- end }}

{{- define "Protocol:Header" }}
{{- $protocol := . }}
{{ "" }}
  {{- range .Methods }}
{{ EnsureNamespace .Request.WireCodingTable }}
__LOCAL extern "C" const fidl_type_t {{ .Request.WireCodingTable.Name }};
{{ EnsureNamespace .Response.WireCodingTable }}
__LOCAL extern "C" const fidl_type_t {{ .Response.WireCodingTable.Name }};
  {{- end }}
{{ "" }}
{{ EnsureNamespace . }}

{{- .Docs }}
class {{ .Name }} final {
  {{ .Name }}() = delete;
 public:
  {{- range .Methods }}
    {{- .Docs }}
    class {{ .Marker.Self }} final {
      {{ .Marker.Self }}() = delete;
    };
  {{- end }}
};

{{- template "Protocol:Details:Header" . }}
{{- template "Protocol:Dispatcher:Header" . }}

{{- range .Methods }}
  {{- if .HasRequest }}
    {{- template "Method:Request:Header" . }}
  {{- end }}
  {{- if .HasResponse }}
    {{- template "Method:Response:Header" . }}
  {{- end }}
{{- end }}

{{- IfdefFuchsia -}}
{{- range .ClientMethods -}}
  {{- template "Method:Result:Header" . }}
  {{- template "Method:UnownedResult:Header" . }}
{{- end }}

{{- template "Protocol:Caller:Header" . }}
{{- template "Protocol:EventHandler:Header" . }}
{{- template "Protocol:SyncClient:Header" . }}
{{- template "Protocol:Interface:Header" . }}
{{- EndifFuchsia -}}

{{- end }}

{{- define "Protocol:Traits:Header" -}}
{{ $protocol := . -}}
{{ range .Methods -}}
{{ $method := . -}}
{{- if .HasRequest }}

template <>
struct IsFidlType<{{ .WireRequest }}> : public std::true_type {};
template <>
struct IsFidlMessage<{{ .WireRequest }}> : public std::true_type {};
{{- if .Request.IsResource }}
{{- IfdefFuchsia -}}
{{- end }}
static_assert(sizeof({{ .WireRequest }})
    == {{ .WireRequest }}::PrimarySize);
{{- range $index, $param := .RequestArgs }}
static_assert(offsetof({{ $method.WireRequest }}, {{ $param.Name }}) == {{ $param.OffsetV1 }});
{{- end }}
{{- if .Request.IsResource }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
{{- if .HasResponse }}

template <>
struct IsFidlType<{{ .WireResponse }}> : public std::true_type {};
template <>
struct IsFidlMessage<{{ .WireResponse }}> : public std::true_type {};
{{- if .Response.IsResource }}
{{- IfdefFuchsia -}}
{{- end }}
static_assert(sizeof({{ .WireResponse }})
    == {{ .WireResponse }}::PrimarySize);
{{- range $index, $param := .ResponseArgs }}
static_assert(offsetof({{ $method.WireResponse }}, {{ $param.Name }}) == {{ $param.OffsetV1 }});
{{- end }}
{{- if .Response.IsResource }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
{{- end }}
{{- end }}

{{- define "Protocol:Source" }}
{{ $protocol := . -}}

{{- range .Methods }}
{{ EnsureNamespace .OrdinalName }}
[[maybe_unused]]
constexpr uint64_t {{ .OrdinalName.Name }} = {{ .Ordinal }}lu;
{{ EnsureNamespace .Request.WireCodingTable }}
extern "C" const fidl_type_t {{ .Request.WireCodingTable.Name }};
{{ EnsureNamespace .Response.WireCodingTable }}
extern "C" const fidl_type_t {{ .Response.WireCodingTable.Name }};
{{- end }}

{{- /* Client-calling functions do not apply to events. */}}
{{- range .ClientMethods -}}
{{ "" }}
    {{- template "Method:Result:Source" . }}
  {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
    {{- template "Method:UnownedResult:Source" . }}
  {{- end }}
{{ "" }}
{{- end }}

{{- range .ClientMethods }}
  {{- if .HasResponse }}
    {{- template "Method:ResponseContext:Source" . }}
  {{- end }}
{{- end }}
{{ template "Protocol:ClientImpl:Source" . }}

{{- if .Events }}
  {{- template "Protocol:EventHandler:Source" . }}
{{- end }}

{{- /* Server implementation */}}
{{ template "Protocol:Dispatcher:Source" . }}

{{- if .Methods }}
  {{- range .TwoWayMethods -}}
    {{- template "Method:CompleterBase:Source" . }}
  {{- end }}

  {{- range .Methods }}

    {{- if .HasRequest }}{{ template "Method:Request:Source" . }}{{ end }}
    {{ "" }}

    {{- if .HasResponse }}{{ template "Method:Response:Source" . }}{{ end }}
    {{ "" }}

  {{- end }}
{{- end }}

{{- end }}
`
