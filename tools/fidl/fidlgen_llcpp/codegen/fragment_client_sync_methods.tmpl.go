// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientSyncMethodsTmpl = `
{{- define "ClientSyncRequestCallerAllocateMethodDefinition" }}
{{ EnsureNamespace "" }}
  {{- if .HasResponse }}
{{- IfdefFuchsia -}}
{{ .WireUnownedResult }}
{{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}_Sync(
     {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return {{ .WireUnownedResult }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
    {{- if .RequestArgs -}}
      , _request_buffer.data, _request_buffer.capacity
    {{- end -}}
      {{- .RequestArgs | CommaParamNames -}},
      _response_buffer.data, _response_buffer.capacity);
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
    auto _res = {{ .WireUnownedResult }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
    {{- if .RequestArgs -}}
      , _request_buffer.data, _request_buffer.capacity
    {{- end }}
      {{- .RequestArgs | CommaParamNames -}});
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
{{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}_Sync({{ .RequestArgs | Params }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return {{ .WireResult }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
      {{- .RequestArgs | CommaParamNames -}}
    );
  }
  return {{ .WireResult }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
{{- EndifFuchsia -}}
  {{- else }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}({{ .RequestArgs | Params }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = {{ .WireResult }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
      {{- .RequestArgs | CommaParamNames -}}
    );
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
{{- EndifFuchsia -}}
  {{- end }}
{{- end }}
`
