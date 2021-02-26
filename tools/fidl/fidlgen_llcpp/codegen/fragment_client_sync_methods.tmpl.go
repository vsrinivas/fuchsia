// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientSyncMethodsTmpl = `
{{- define "ClientSyncRequestCallerAllocateMethodDefinition" }}
  {{- if .HasResponse }}
#ifdef __Fuchsia__
{{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }}
{{ .LLProps.ProtocolName.Name }}::ClientImpl::{{ .Name }}_Sync(
     {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return UnownedResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}>(_channel->handle())
    {{- if .Request -}}
      , _request_buffer.data, _request_buffer.capacity
    {{- end -}}
      {{- .Request | CommaParamNames -}},
      _response_buffer.data, _response_buffer.capacity);
  }
  return {{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
#endif
  {{- else }}{{ if .Request }}
#ifdef __Fuchsia__
::fidl::Result {{ .LLProps.ProtocolName.Name }}::ClientImpl::{{ .Name }}(
    {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = UnownedResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}>(_channel->handle())
    {{- if .Request -}}
      , _request_buffer.data, _request_buffer.capacity
    {{- end }}
      {{- .Request | CommaParamNames -}});
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
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}
{{ .LLProps.ProtocolName.Name }}::ClientImpl::{{ .Name }}_Sync({{ .Request | Params }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return ResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}>(_channel->handle())
      {{- .Request | CommaParamNames -}}
    );
  }
  return {{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
#endif
  {{- else }}
#ifdef __Fuchsia__
::fidl::Result {{ .LLProps.ProtocolName.Name }}::ClientImpl::{{ .Name }}({{ .Request | Params }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = ResultOf::{{ .Name }}(
      ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}>(_channel->handle())
      {{- .Request | CommaParamNames -}}
    );
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
#endif
  {{- end }}
{{- end }}
`
