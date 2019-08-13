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

{{- define "SyncRequestCallerAllocateMethodSignature" -}}
  {{- if .Response -}}
{{ .Name }}_Deprecated({{ template "CallerBufferParams" .Request }}{{ if .Request }}, {{ end }}::fidl::BytePart _response_buffer, {{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}_Deprecated({{ template "CallerBufferParams" .Request }})
  {{- end -}}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodSignature" -}}
  {{- if .Response -}}
{{ .Name }}_Deprecated(zx::unowned_channel _client_end, {{ template "CallerBufferParams" .Request }}{{ if .Request }}, {{ end }}::fidl::BytePart _response_buffer, {{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}_Deprecated(zx::unowned_channel _client_end, {{ template "CallerBufferParams" .Request }})
  {{- end -}}
{{- end }}

{{- define "SyncRequestCallerAllocateMethodArgumentsNew" -}}
{{ template "CallerBufferParams" .Request }}{{ if .HasResponse }}{{ if .Request }}, {{ end }}::fidl::BytePart _response_buffer{{ end }}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodArgumentsNew" -}}
zx::unowned_channel _client_end{{ if .Request }}, {{ end }}{{ template "CallerBufferParams" .Request }}{{ if .HasResponse }}, ::fidl::BytePart _response_buffer{{ end }}
{{- end }}

{{- define "SyncRequestCallerAllocateMethodDefinition" }}
{{ if .HasResponse -}}
  ::fidl::DecodeResult<{{ .LLProps.InterfaceName }}::{{ .Name }}Response>
{{- else -}}
  zx_status_t
{{- end }} {{ .LLProps.InterfaceName }}::SyncClient::{{ template "SyncRequestCallerAllocateMethodSignature" . }} {
  return {{ .LLProps.InterfaceName }}::Call::{{ .Name }}_Deprecated(zx::unowned_channel(this->channel_)
    {{- if or .Request .Response }}, {{ end }}
    {{- if .Request -}}
      std::move(_request_buffer), {{ template "SyncClientMoveParams" .Request }}
    {{- end }}
    {{- if and .Request .Response }}, {{ end }}
    {{- if .Response -}}
      std::move(_response_buffer), {{ range $index, $param := .Response -}}
        {{- if $index }}, {{ end -}} out_{{ $param.Name }}
      {{- end -}}
    {{- end -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodDefinition" }}
{{ if .HasResponse -}}
  ::fidl::DecodeResult<{{ .LLProps.InterfaceName }}::{{ .Name }}Response>
{{- else -}}
  zx_status_t
{{- end }} {{ .LLProps.InterfaceName }}::Call::{{ template "StaticCallSyncRequestCallerAllocateMethodSignature" . }} {
  {{- if not .Request }}
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof({{ .Name }}Request)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  {{- else }}
  if (_request_buffer.capacity() < {{ .Name }}Request::PrimarySize) {
    {{- if .HasResponse }}
    return ::fidl::DecodeResult<{{ .Name }}Response>(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::internal::kErrorRequestBufferTooSmall);
    {{- else }}
    return ZX_ERR_BUFFER_TOO_SMALL;
    {{- end }}
  }
  {{- end }}
  {{- if .LLProps.LinearizeRequest }}
  {{ .Name }}Request _request = {};
  {{- else }}
  auto& _request = *reinterpret_cast<{{ .Name }}Request*>(_request_buffer.data());
  {{- end }}
  _request._hdr.ordinal = {{ .Ordinals.Write.Name }};
  {{- template "FillRequestStructMembers" .Request -}}

  {{- if .LLProps.LinearizeRequest }}
  auto _linearize_result = ::fidl::Linearize(&_request, std::move(_request_buffer));
  if (_linearize_result.status != ZX_OK) {
    {{- if .HasResponse }}
    return ::fidl::DecodeResult<{{ .Name }}Response>(_linearize_result.status, _linearize_result.error);
    {{- else }}
    return _linearize_result.status;
    {{- end }}
  }
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request = std::move(_linearize_result.message);
  {{- else }}
  _request_buffer.set_actual(sizeof({{ .Name }}Request));
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request(std::move(_request_buffer));
  {{- end }}
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    {{- if .HasResponse }}
    return ::fidl::DecodeResult<{{ .Name }}Response>(_encode_request_result.status, _encode_request_result.error);
    {{- else }}
    return _encode_request_result.status;
    {{- end }}
  }
  {{- if .HasResponse }}
    {{- if not .Response }}
  constexpr uint32_t _kReadAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving>();
  FIDL_ALIGNDECL uint8_t _read_bytes[_kReadAllocSize];
  ::fidl::BytePart _response_buffer(_read_bytes, sizeof(_read_bytes));
    {{- end }}
  auto _call_result = ::fidl::Call<{{ .Name }}Request, {{ .Name }}Response>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(_response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<{{ .Name }}Response>(_call_result.status, _call_result.error);
  }
  auto _decode_result = ::fidl::Decode(std::move(_call_result.message));
  if (_decode_result.status != ZX_OK) {
    return _decode_result;
  }
    {{- if .Response }}
  auto& _response = *_decode_result.message.message();
    {{- end }}
    {{- template "ReturnResponseStructMembers" .Response }}
  return _decode_result;
  {{- else }}
  return ::fidl::Write(std::move(_client_end), std::move(_encode_request_result.message));
  {{- end }}
}
{{- end }}

{{- define "SyncRequestCallerAllocateMethodDefinitionNew" }}
{{ if .HasResponse -}} template <> {{- end }}
{{ .LLProps.InterfaceName }}::UnownedResultOf::{{ .Name }}_Impl {{- if .HasResponse -}} <{{ .LLProps.InterfaceName }}::{{ .Name }}Response> {{- end }}::{{ .Name }}_Impl(
  {{- template "StaticCallSyncRequestCallerAllocateMethodArgumentsNew" . }}) {
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
  {{- template "SyncRequestCallerAllocateMethodArgumentsNew" . }}) {
  return UnownedResultOf::{{ .Name }}(zx::unowned_channel(this->channel_)
    {{- if .Request -}}
      , std::move(_request_buffer), {{ template "SyncClientMoveParams" .Request }}
    {{- end }}
    {{- if .HasResponse -}}
      , std::move(_response_buffer)
    {{- end -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodDefinitionNew" }}
{{ .LLProps.InterfaceName }}::UnownedResultOf::{{ .Name }} {{ .LLProps.InterfaceName }}::Call::{{ .Name }}(
  {{- template "StaticCallSyncRequestCallerAllocateMethodArgumentsNew" . }}) {
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
