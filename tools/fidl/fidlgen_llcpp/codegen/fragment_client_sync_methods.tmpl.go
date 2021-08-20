// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientSyncMethodsTmpl = `
{{- define "ClientSyncRequestCallerAllocateMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- $base_args := (List) }}
{{- if .HasResponse }}
  {{- $base_args = (printf "::fidl::UnownedClientEnd<%s>(_channel->handle())" .Protocol) }}
{{- end }}
{{- if .RequestArgs }}
  {{- $base_args = (List $base_args "_request_buffer.data" "_request_buffer.capacity") }}
{{- end }}
{{- $base_args = (List $base_args .RequestArgs) }}

{{- if .HasResponse }}
{{- IfdefFuchsia -}}
{{ .WireUnownedResult }}
{{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}_Sync(
     {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannelForSyncCall()) {
    return {{ .WireUnownedResult }}(
        {{- RenderForwardParams $base_args "_response_buffer.data" "_response_buffer.capacity" }});
  }
  return {{ .WireUnownedResult }}(::fidl::Result::Unbound());
}
{{- EndifFuchsia -}}
{{- else }}
{{- if .RequestArgs }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
    {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  ::fidl::UnownedEncodedMessage<{{ .WireRequest }}> _request({{ RenderForwardParams $base_args }});
  return ::fidl::internal::ClientBase::SendOneWay(_request.GetOutgoingMessage());
}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
{{- end }}





{{- define "ClientSyncRequestManagedMethodDefinition" }}
{{ EnsureNamespace "" }}

{{- if .HasResponse }}
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
{{- else }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}({{ RenderParams .RequestArgs }}) {
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
  ::fidl::OwnedEncodedMessage<{{ .WireRequest }}> _request(
      {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" .RequestArgs }});
  return ::fidl::internal::ClientBase::SendOneWay(_request.GetOutgoingMessage());
}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
`
