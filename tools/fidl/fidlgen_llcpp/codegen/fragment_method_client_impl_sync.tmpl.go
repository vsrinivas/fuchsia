// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentMethodClientImplSyncTmpl = `
{{- define "Method:ClientImplSyncCallerAllocateArguments:Helper" -}}
{{- $args := (List) }}
{{- if .RequestArgs }}
  {{- $args = (List $args "::fidl::BufferSpan _request_buffer" .RequestArgs) }}
{{- end }}
{{- if .HasResponse }}
  {{- $args = (List $args "::fidl::BufferSpan _response_buffer") }}
{{- end }}
{{- RenderParams $args }}
{{- end }}



{{- define "Method:ClientImplSync:Header" }}
  {{- /* Sync managed flavor */}}
  {{- .Docs }}
  {{ if .DocComments }}
    //
  {{- end }}
  // Synchronous variant of |{{ $.Name }}.{{ .Name }}()|.
  // {{- template "Method:ClientAllocationComment:Helper" . }}
  {{ .WireResult }} {{ .Name }}_Sync({{ RenderParams .RequestArgs }});

  {{- /* Sync caller-allocate flavor */}}
  {{- if or .RequestArgs .ResponseArgs }}
    {{ .Docs }}
    {{- if .DocComments }}
      //
    {{- end }}
    // Synchronous variant of |{{ $.Name }}.{{ .Name }}()|.
    // Caller provides the backing storage for FIDL message via request and
    // response buffers.
    {{ .WireUnownedResult }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}(
        {{- template "Method:ClientImplSyncCallerAllocateArguments:Helper" . }});
  {{- end }}
{{- end }}



{{- define "Method:ClientImplSyncCallerAllocate:Source" }}
  {{ EnsureNamespace "" }}

  {{- $base_args := (printf "::fidl::UnownedClientEnd<%s>(_channel->handle())" .Protocol) }}
  {{- if .RequestArgs }}
    {{- $base_args = (List $base_args "_request_buffer.data" "_request_buffer.capacity") }}
  {{- end }}
  {{- $base_args = (List $base_args .RequestArgs) }}

  {{- IfdefFuchsia -}}

  {{ .WireUnownedResult }}
  {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}_Sync(
      {{- template "Method:ClientImplSyncCallerAllocateArguments:Helper" . }}) {
    if (auto _channel = ::fidl::internal::ClientBase::GetChannelForSyncCall()) {
      return {{ .WireUnownedResult }}(
          {{- RenderForwardParams $base_args "_response_buffer.data" "_response_buffer.capacity" }});
    }
    return {{ .WireUnownedResult }}(::fidl::Result::Unbound());
  }

  {{- EndifFuchsia -}}
{{- end }}



{{- define "Method:ClientImplSyncManaged:Source" }}
  {{ EnsureNamespace "" }}

  {{- IfdefFuchsia -}}
  {{ .WireResult }}
  {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}_Sync({{ RenderParams .RequestArgs }}) {
    if (auto _channel = ::fidl::internal::ClientBase::GetChannelForSyncCall()) {
      return {{ .WireResult }}(
        {{- RenderForwardParams (printf "::fidl::UnownedClientEnd<%s>(_channel->handle())" .Protocol)
                                .RequestArgs }});
    }
    return {{ .WireResult }}(::fidl::Result::Unbound());
  }

  {{- EndifFuchsia -}}
{{- end }}




{{- define "Method:ClientImplSync:Source" }}
  {{- template "Method:ClientImplSyncManaged:Source" . }}
  {{- if or .RequestArgs .ResponseArgs }}
    {{- template "Method:ClientImplSyncCallerAllocate:Source" . }}
  {{- end }}
{{- end }}
`
