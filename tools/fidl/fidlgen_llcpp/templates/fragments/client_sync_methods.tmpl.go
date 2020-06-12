// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ClientSyncMethods = `
{{- define "ClientSyncRequestCallerAllocateMethodDefinition" }}
  {{- if .HasResponse }}
{{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}_Sync({{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _binding = ::fidl::internal::ClientBase::GetBinding()) {
    return UnownedResultOf::{{ .Name }}(_binding->channel()
      {{- if .Request -}}
        , std::move(_request_buffer), {{ template "SyncClientMoveParams" .Request }}
      {{- end -}}
        , std::move(_response_buffer));
  }
  return ::fidl::StatusAndError(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
  {{- else }}{{ if .Request }}
::fidl::StatusAndError {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}({{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  if (auto _binding = ::fidl::internal::ClientBase::GetBinding()) {
    auto _res = UnownedResultOf::{{ .Name }}(_binding->channel()
      {{- if .Request -}}
        , std::move(_request_buffer), {{ template "SyncClientMoveParams" .Request }}
      {{- end -}}
    );
    return ::fidl::StatusAndError(_res.status(), _res.error());
  }
  return ::fidl::StatusAndError(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
  {{- end }}{{ end }}
{{- end }}

{{- define "ClientSyncRequestManagedMethodDefinition" }}
  {{- if .HasResponse }}
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}_Sync(
  {{- template "SyncRequestManagedMethodArguments" . }}) {
  if (auto _binding = ::fidl::internal::ClientBase::GetBinding()) {
    return ResultOf::{{ .Name }}(_binding->channel()
      {{- if .Request }}, {{ end }}
      {{- template "SyncClientMoveParams" .Request -}}
    );
  }
  return ::fidl::StatusAndError(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
  {{- else }}
::fidl::StatusAndError {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}(
  {{- template "SyncRequestManagedMethodArguments" . }}) {
  if (auto _binding = ::fidl::internal::ClientBase::GetBinding()) {
    auto _res = ResultOf::{{ .Name }}(_binding->channel()
      {{- if .Request }}, {{ end }}
      {{- template "SyncClientMoveParams" .Request -}}
    );
    return ::fidl::StatusAndError(_res.status(), _res.error());
  }
  return ::fidl::StatusAndError(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
  {{- end }}
{{- end }}
`
