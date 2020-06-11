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
{{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }}_Impl {{- if .HasResponse -}} <{{ .LLProps.ProtocolName }}::{{ .Name }}Response> {{- end }}::{{ .Name }}_Impl(
  {{- template "StaticCallSyncRequestCallerAllocateMethodArguments" . }}) {
  {{- if not .Request }}
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof({{ .Name }}Request)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  {{- else }}
  if (_request_buffer.capacity() < {{ .Name }}Request::PrimarySize) {
    {{- if .HasResponse }}
    Super::SetFailure(::fidl::DecodeResult<{{ .Name }}Response>(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall));
    {{- else }}
    Super::status_ = ZX_ERR_BUFFER_TOO_SMALL;
    Super::error_ = ::fidl::kErrorRequestBufferTooSmall;
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
  auto _encode_result = ::fidl::LinearizeAndEncode<{{ .Name }}Request>(&_request, std::move(_request_buffer));
  if (_encode_result.status != ZX_OK) {
    Super::SetFailure(std::move(_encode_result));
    return;
  }
  {{- else }}
  _request_buffer.set_actual(sizeof({{ .Name }}Request));
  ::fidl::DecodedMessage<{{ .Name }}Request> _msg(std::move(_request_buffer));
  auto _encode_result = ::fidl::Encode<{{ .Name }}Request>(std::move(_msg));
  if (_encode_result.status != ZX_OK) {
    Super::SetFailure(std::move(_encode_result));
    return;
  }
  {{- end }}
  ::fidl::EncodedMessage<{{ .Name }}Request> _encoded_request = std::move(_encode_result.message);

  {{- if .HasResponse }}
  Super::SetResult(
      {{ .LLProps.ProtocolName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_encoded_request){{ end -}}
      , std::move(_response_buffer)));
  {{- else }}
  Super::operator=(
      {{ .LLProps.ProtocolName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_encoded_request){{ end -}}
  ));
  {{- end }}
}

{{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::SyncClient::{{ .Name }}(
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
{{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::Call::{{ .Name }}(
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
