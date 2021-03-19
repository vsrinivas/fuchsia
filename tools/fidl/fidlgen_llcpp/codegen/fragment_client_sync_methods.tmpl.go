// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientSyncMethodsTmpl = `
{{- define "ClientSyncRequestCallerAllocateMethodDefinition" }}
  {{- if .HasResponse }}
#ifdef __Fuchsia__
{{ .Protocol }}::UnownedResultOf::{{ .Name }}
{{ .Protocol.Name }}::ClientImpl::{{ .Name }}_Sync(
     {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return UnownedResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
    {{- if .RequestArgs -}}
      , _request_buffer.data, _request_buffer.capacity
    {{- end -}}
      {{- .RequestArgs | CommaParamNames -}},
      _response_buffer.data, _response_buffer.capacity);
  }
  return {{ .Protocol }}::UnownedResultOf::{{ .Name }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
#endif
  {{- else }}{{ if .RequestArgs }}
#ifdef __Fuchsia__
::fidl::Result {{ .Protocol.Name }}::ClientImpl::{{ .Name }}(
    {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = UnownedResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
    {{- if .RequestArgs -}}
      , _request_buffer.data, _request_buffer.capacity
    {{- end }}
      {{- .RequestArgs | CommaParamNames -}});
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
#endif
  {{- end }}{{ end }}
{{- end }}

{{- define "ClientSyncRequestManagedMethodDefinition" }}
  {{- if .HasResponse }}
#ifdef __Fuchsia__
{{ .Protocol }}::ResultOf::{{ .Name }}
{{ .Protocol.Name }}::ClientImpl::{{ .Name }}_Sync({{ .RequestArgs | Params }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return ResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
      {{- .RequestArgs | CommaParamNames -}}
    );
  }
  return {{ .Protocol }}::ResultOf::{{ .Name }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
#endif
  {{- else }}
#ifdef __Fuchsia__
::fidl::Result {{ .Protocol.Name }}::ClientImpl::{{ .Name }}({{ .RequestArgs | Params }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = ResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .Protocol }}>(_channel->handle())
      {{- .RequestArgs | CommaParamNames -}}
    );
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
#endif
  {{- end }}
{{- end }}
`
