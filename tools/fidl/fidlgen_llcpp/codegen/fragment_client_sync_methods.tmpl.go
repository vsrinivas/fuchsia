// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientSyncMethodsTmpl = `
{{- define "ClientSyncRequestCallerAllocateMethodDefinition" }}
  {{- if .HasResponse }}
{{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}_Sync(
     {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return UnownedResultOf::{{ .Name }}(_channel->handle()
    {{- if .Request -}}
      , _request_buffer.data(), _request_buffer.capacity()
    {{- end -}}
      {{- template "CommaPassthroughMessageParams" .Request -}},
      _response_buffer.data(), _response_buffer.capacity());
  }
  return {{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
  {{- else }}{{ if .Request }}
::fidl::Result {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}({{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = UnownedResultOf::{{ .Name }}(_channel->handle()
    {{- if .Request -}}
      , _request_buffer.data(), _request_buffer.capacity()
    {{- end }}
      {{- template "CommaPassthroughMessageParams" .Request -}});
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
  {{- end }}{{ end }}
{{- end }}

{{- define "ClientSyncRequestManagedMethodDefinition" }}
  {{- if .HasResponse }}
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}_Sync(
  {{- template "SyncRequestManagedMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    return ResultOf::{{ .Name }}(_channel->handle()
      {{- template "CommaPassthroughMessageParams" .Request -}}
    );
  }
  return {{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}(
    ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound));
}
  {{- else }}
::fidl::Result {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}(
  {{- template "SyncRequestManagedMethodArguments" . }}) {
  if (auto _channel = ::fidl::internal::ClientBase::GetChannel()) {
    auto _res = ResultOf::{{ .Name }}(_channel->handle()
      {{- template "CommaPassthroughMessageParams" .Request -}}
    );
    return ::fidl::Result(_res.status(), _res.error());
  }
  return ::fidl::Result(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
  {{- end }}
{{- end }}
`
