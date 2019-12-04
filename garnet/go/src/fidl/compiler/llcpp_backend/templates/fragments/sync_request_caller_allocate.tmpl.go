// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SyncRequestCallerAllocate = `
{{- define "CallerBufferParams" -}}
{{- if . -}}
::fidl::BytePart _request_buffer, {{ range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.LLDecl }} {{ $param.Name }}
  {{- end -}}
{{- end -}}
{{- end }}

{{- define "SyncRequestCallerAllocateMethodArguments" -}}
{{ template "CallerBufferParams" .Request }}{{ if .HasResponse }}{{ if .Request }}, {{ end }}::fidl::BytePart _response_buffer{{ end }}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodArguments" -}}
::zx::unowned_channel _client_end{{ if .Request }}, {{ end }}{{ template "CallerBufferParams" .Request }}{{ if .HasResponse }}, ::fidl::BytePart _response_buffer{{ end }}
{{- end }}

{{- define "SyncRequestCallerAllocateMethodDefinition" }}
{{ if .HasResponse -}} template <> {{- end }}
{{ .LLProps.InterfaceName }}::UnownedResultOf::{{ .Name }}_Impl {{- if .HasResponse -}} <{{ .LLProps.InterfaceName }}::{{ .Name }}Response> {{- end }}::{{ .Name }}_Impl(
  {{- template "StaticCallSyncRequestCallerAllocateMethodArguments" . }}) {
  {{- if not .Request }}
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof({{ .Name }}Request)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  {{- else }}
  if (_request_buffer.capacity() < {{ .Name }}Request::PrimarySize) {
    {{- if .HasResponse }}
    Super::SetFailure(::fidl::DecodeResult<{{ .Name }}Response>(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::internal::kErrorRequestBufferTooSmall));
    {{- else }}
    Super::status_ = ZX_ERR_BUFFER_TOO_SMALL;
    Super::error_ = ::fidl::internal::kErrorRequestBufferTooSmall;
    {{- end }}
    return;
  }
  {{- end }}
  {{- if .LLProps.LinearizeRequest }}
  {{ .Name }}Request _request = {};
  {{- else }}
  memset(_request_buffer.data(), 0, {{ .Name }}Request::PrimarySize);
    {{- if .Request }}
  auto& _request = *reinterpret_cast<{{ .Name }}Request*>(_request_buffer.data());
    {{- end }}
  {{- end }}
  {{- template "FillRequestStructMembers" .Request -}}

  {{- if .LLProps.LinearizeRequest }}
  auto _linearize_result = ::fidl::Linearize(&_request, std::move(_request_buffer));
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request = std::move(_linearize_result.message);
  {{- else }}
  _request_buffer.set_actual(sizeof({{ .Name }}Request));
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request(std::move(_request_buffer));
  {{- end }}

  {{- if .HasResponse }}
  Super::SetResult(
      {{ .LLProps.InterfaceName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_decoded_request){{ end -}}
      , std::move(_response_buffer)));
  {{- else }}
  Super::operator=(
      {{ .LLProps.InterfaceName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_decoded_request){{ end -}}
  ));
  {{- end }}
}

{{ .LLProps.InterfaceName }}::UnownedResultOf::{{ .Name }} {{ .LLProps.InterfaceName }}::SyncClient::{{ .Name }}(
  {{- template "SyncRequestCallerAllocateMethodArguments" . }}) {
  return UnownedResultOf::{{ .Name }}(::zx::unowned_channel(this->channel_)
    {{- if .Request -}}
      , std::move(_request_buffer), {{ template "SyncClientMoveParams" .Request }}
    {{- end }}
    {{- if .HasResponse -}}
      , std::move(_response_buffer)
    {{- end -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodDefinition" }}
{{ .LLProps.InterfaceName }}::UnownedResultOf::{{ .Name }} {{ .LLProps.InterfaceName }}::Call::{{ .Name }}(
  {{- template "StaticCallSyncRequestCallerAllocateMethodArguments" . }}) {
  return UnownedResultOf::{{ .Name }}(std::move(_client_end)
    {{- if .Request -}}
      , std::move(_request_buffer), {{ template "SyncClientMoveParams" .Request }}
    {{- end }}
    {{- if .HasResponse -}}
      , std::move(_response_buffer)
    {{- end -}});
}
{{- end }}
`
