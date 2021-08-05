// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentProtocolTmpl = `
{{- define "ProtocolForwardDeclaration" }}
{{ EnsureNamespace . }}
class {{ .Name }};
{{- end }}


{{- define "ClientAllocationComment" -}}
{{- if SyncCallTotalStackSizeV1 . }} Allocates {{ SyncCallTotalStackSizeV1 . }} bytes of {{ "" }}
{{- if not .Request.ClientAllocationV1.IsStack -}} response {{- else -}}
  {{- if not .Response.ClientAllocationV1.IsStack -}} request {{- else -}} message {{- end -}}
{{- end }} buffer on the stack. {{- end }}
{{- if and .Request.ClientAllocationV1.IsStack .Response.ClientAllocationV1.IsStack -}}
{{ "" }} No heap allocation necessary.
{{- else }}
  {{- if not .Request.ClientAllocationV1.IsStack }} Request is heap-allocated. {{- end }}
  {{- if not .Response.ClientAllocationV1.IsStack }} Response is heap-allocated. {{- end }}
{{- end }}
{{- end }}

{{- define "ProtocolDeclaration" }}
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

{{- template "ProtocolDetailsDeclaration" . }}
{{- template "ProtocolDispatcherDeclaration" . }}

{{- range .Methods }}
  {{- if .HasRequest }}
    {{- template "MethodRequestDeclaration" . }}
  {{- end }}
  {{- if .HasResponse }}
    {{- template "MethodResponseDeclaration" . }}
  {{- end }}
{{- end }}

{{- IfdefFuchsia -}}
{{- range .ClientMethods -}}
  {{- template "MethodResultDeclaration" . }}
  {{- template "MethodUnownedResultDeclaration" . }}
{{- end }}

{{- template "ProtocolCallerDeclaration" . }}
{{- template "ProtocolEventHandlerDeclaration" . }}
{{- template "ProtocolSyncClientDeclaration" . }}
{{- template "ProtocolInterfaceDeclaration" . }}
{{- EndifFuchsia -}}

{{- end }}

{{- define "ProtocolTraits" -}}
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
static_assert(offsetof({{ $method.WireRequest }}, {{ $param.Name }}) == {{ $param.Offset }});
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
static_assert(offsetof({{ $method.WireResponse }}, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- if .Response.IsResource }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
{{- end }}
{{- end }}

{{- define "ProtocolDefinition" }}
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
    {{- template "MethodResultDefinition" . }}
  {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
    {{- template "MethodUnownedResultDefinition" . }}
  {{- end }}
{{ "" }}
{{- end }}

{{- range .ClientMethods }}
{{ "" }}
  {{- template "ClientSyncRequestManagedMethodDefinition" . }}
  {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
    {{- template "ClientSyncRequestCallerAllocateMethodDefinition" . }}
  {{- end }}
  {{- if .HasResponse }}
{{ "" }}
    {{- template "MethodResponseContextDefinition" . }}
    {{- template "ClientAsyncRequestManagedMethodDefinition" . }}
  {{- end }}
{{- end }}
{{ template "ProtocolClientImplDefinition" . }}
{{ "" }}

{{- if .Events }}
  {{- template "EventHandlerHandleOneEventMethodDefinition" . }}
{{- end }}

{{- /* Server implementation */}}
{{ template "ProtocolDispatcherDefinition" . }}

{{- if .Methods }}
  {{- range .TwoWayMethods -}}
    {{- template "MethodDetailsDefinition" . }}
  {{- end }}

  {{- range .Methods }}

    {{- if .HasRequest }}{{ template "MethodRequestDefinition" . }}{{ end }}
    {{ "" }}

    {{- if .HasResponse }}{{ template "MethodResponseDefinition" . }}{{ end }}
    {{ "" }}

  {{- end }}
{{- end }}

{{- end }}
`
