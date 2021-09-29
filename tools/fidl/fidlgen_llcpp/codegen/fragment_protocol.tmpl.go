// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentProtocolTmpl = `
{{- define "Protocol:ForwardDeclaration:MessagingHeader" }}
  {{ EnsureNamespace . }}
  class {{ .Name }};
{{- end }}


{{- define "Method:ClientAllocationComment:Helper" -}}
  {{- if SyncCallTotalStackSizeV2 . -}}
    Allocates {{ SyncCallTotalStackSizeV2 . }} bytes of {{ "" -}}
    {{- if not .Request.ClientAllocationV2.IsStack -}}
      response
    {{- else -}}
      {{- if not .Response.ClientAllocationV2.IsStack -}}
        request
      {{- else -}}
        message
      {{- end -}}
    {{- end }} buffer on the stack.
  {{- end }}
  {{- if and .Request.ClientAllocationV2.IsStack .Response.ClientAllocationV2.IsStack -}}
    {{ "" }} No heap allocation necessary.
  {{- else }}
    {{- if not .Request.ClientAllocationV2.IsStack }} Request is heap-allocated. {{- end }}
    {{- if not .Response.ClientAllocationV2.IsStack }} Response is heap-allocated. {{- end }}
  {{- end }}
{{- end }}

{{- define "Protocol:MessagingHeader" }}
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

{{- template "Protocol:Details:MessagingHeader" . }}
{{- template "Protocol:Dispatcher:MessagingHeader" . }}

{{- range .Methods }}
  {{- if .HasRequest }}
    {{- template "Method:Request:MessagingHeader" . }}
  {{- end }}
  {{- if .HasResponse }}
    {{- template "Method:Response:MessagingHeader" . }}
  {{- end }}
{{- end }}

{{- IfdefFuchsia -}}
{{- range .ClientMethods -}}
  {{- template "Method:Result:MessagingHeader" . }}
  {{- template "Method:UnownedResult:MessagingHeader" . }}
{{- end }}

{{- template "Protocol:Caller:MessagingHeader" . }}
{{- template "Protocol:EventHandler:MessagingHeader" . }}
{{- template "Protocol:SyncClient:MessagingHeader" . }}
{{- template "Protocol:Interface:MessagingHeader" . }}
{{- EndifFuchsia -}}

{{- end }}

{{- define "Protocol:Traits:MessagingHeader" -}}
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
static_assert(offsetof({{ $method.WireRequest }}, {{ $param.Name }}) == {{ $param.OffsetV2 }});
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
static_assert(offsetof({{ $method.WireResponse }}, {{ $param.Name }}) == {{ $param.OffsetV2 }});
{{- end }}
{{- if .Response.IsResource }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
{{- end }}
{{- end }}

{{- define "Protocol:MessagingSource" }}
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
    {{- template "Method:Result:MessagingSource" . }}
  {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
    {{- template "Method:UnownedResult:MessagingSource" . }}
  {{- end }}
{{ "" }}
{{- end }}

{{- range .ClientMethods }}
  {{- if .HasResponse }}
    {{- template "Method:ResponseContext:MessagingSource" . }}
  {{- end }}
{{- end }}
{{ template "Protocol:ClientImpl:MessagingSource" . }}

{{- if .Events }}
  {{- template "Protocol:EventHandler:MessagingSource" . }}
{{- end }}

{{- /* Server implementation */}}
{{ template "Protocol:Dispatcher:MessagingSource" . }}

{{- if .Methods }}
  {{- range .TwoWayMethods -}}
    {{- template "Method:CompleterBase:MessagingSource" . }}
  {{- end }}

  {{- range .Methods }}

    {{- if .HasRequest }}{{ template "Method:Request:MessagingSource" . }}{{ end }}
    {{ "" }}

    {{- if .HasResponse }}{{ template "Method:Response:MessagingSource" . }}{{ end }}
    {{ "" }}

  {{- end }}
{{- end }}

{{- end }}
`
