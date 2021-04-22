// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientSyncMethodsTmpl = `
{{- define "ClientSyncRequestCallerAllocateMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- $base_args := (printf "::fidl::UnownedClientEnd<%s>(_channel->handle())" .Protocol) }}
{{- if .RequestArgs }}
  {{- $base_args = (List $base_args "_request_buffer.data" "_request_buffer.capacity") }}
{{- end }}
{{- $base_args = (List $base_args .RequestArgs) }}

  {{- if .HasResponse }}
{{- IfdefFuchsia -}}
{{ .WireUnownedResult }}
{{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}_Sync(
     {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return {{ .WireUnownedResult }}({{ RenderForwardParams $base_args "_response_buffer.data" "_response_buffer.capacity" }});
  }
  return {{ .WireUnownedResult }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
{{- EndifFuchsia -}}
  {{- else }}{{ if .RequestArgs }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
    {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = {{ .WireUnownedResult }}({{ RenderForwardParams $base_args }});
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
{{- EndifFuchsia -}}
  {{- end }}{{ end }}
{{- end }}

{{- define "ClientSyncRequestManagedMethodDefinition" }}
{{ EnsureNamespace "" }}

  {{- if .HasResponse }}
{{- IfdefFuchsia -}}
{{ .WireResult }}
{{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}_Sync({{ RenderCalleeParams .RequestArgs }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return {{ .WireResult }}(
      {{- RenderForwardParams (printf "::fidl::UnownedClientEnd<%s>(_channel->handle())" .Protocol)
                              .RequestArgs }});
  }
  return {{ .WireResult }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
{{- EndifFuchsia -}}
  {{- else }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}({{ RenderCalleeParams .RequestArgs }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = {{ .WireResult }}(
      {{- RenderForwardParams (printf "::fidl::UnownedClientEnd<%s>(_channel->handle())" .Protocol)
                              .RequestArgs }});
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
{{- EndifFuchsia -}}
  {{- end }}
{{- end }}
`
