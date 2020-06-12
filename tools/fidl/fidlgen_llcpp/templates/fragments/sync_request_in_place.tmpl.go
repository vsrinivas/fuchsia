// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SyncRequestInPlace = `
{{- define "StaticCallSyncRequestInPlaceMethodSignatureDecodedMessage" -}}
{{ .Name }}(::zx::unowned_channel _client_end, ::fidl::DecodedMessage<{{ .Name }}Request> params{{ if .HasResponse }}, ::fidl::BytePart response_buffer{{ end }})
{{- end }}
{{- define "StaticCallSyncRequestInPlaceMethodSignatureEncodedMessage" -}}
{{ .Name }}(::zx::unowned_channel _client_end{{ if .Request }}, ::fidl::EncodedMessage<{{ .Name }}Request> params{{ end }}{{ if .HasResponse }}, ::fidl::BytePart response_buffer{{ end }})
{{- end }}

{{- define "StaticCallSyncRequestInPlaceMethodDefinition" }}
{{ if .Request }}
{{- $protocol_name := .LLProps.ProtocolName }}
{{ if .HasResponse -}}
::fidl::DecodeResult<{{ $protocol_name }}::{{ .Name }}Response>
{{- else -}}
::fidl::internal::StatusAndError
{{- end }} {{ $protocol_name }}::InPlace::{{ template "StaticCallSyncRequestInPlaceMethodSignatureDecodedMessage" . }} {
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
  {{- if .HasResponse }}
    return ::fidl::DecodeResult<{{ $protocol_name }}::{{ .Name }}Response>::FromFailure(
        std::move(_encode_request_result));
  {{- else }}
    return ::fidl::internal::StatusAndError::FromFailure(
        std::move(_encode_request_result));
  {{- end }}
  }
  return {{ .Name }}(std::move(_client_end), std::move(_encode_request_result.message){{ if .HasResponse }}, std::move(response_buffer){{ end }});
}
{{ end }}

{{- $protocol_name := .LLProps.ProtocolName }}
{{ if .HasResponse -}}
::fidl::DecodeResult<{{ $protocol_name }}::{{ .Name }}Response>
{{- else -}}
::fidl::internal::StatusAndError
{{- end }} {{ $protocol_name }}::InPlace::{{ template "StaticCallSyncRequestInPlaceMethodSignatureEncodedMessage" . }} {
  {{- if not .Request }}
  constexpr uint32_t _write_num_bytes = sizeof({{ .Name }}Request);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::EncodedMessage<{{ .Name }}Request> params(std::move(_request_buffer));
  {{- end }}
  {{ $protocol_name }}::SetTransactionHeaderFor::{{ .Name }}Request(params);
  {{- if .HasResponse }}
  auto _call_result = ::fidl::Call<{{ .Name }}Request, {{ .Name }}Response>(
    std::move(_client_end), std::move(params), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<{{ $protocol_name }}::{{ .Name }}Response>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
  {{- else }}
  zx_status_t _write_status =
      ::fidl::Write(std::move(_client_end), std::move(params));
  if (_write_status != ZX_OK) {
    return ::fidl::internal::StatusAndError(_write_status, ::fidl::kErrorWriteFailed);
  } else {
    return ::fidl::internal::StatusAndError(ZX_OK, nullptr);
  }
  {{- end }}
}
{{- end }}
`
