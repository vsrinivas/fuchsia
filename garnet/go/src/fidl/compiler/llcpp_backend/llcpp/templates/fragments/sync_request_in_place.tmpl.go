// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SyncRequestInPlace = `
{{- define "SyncRequestInPlaceMethodSignature" -}}
{{ .Name }}({{ if .Request }}::fidl::DecodedMessage<{{ .Name }}Request> params{{ if .Response }}, {{ end }}{{ end }}{{ if .Response }}::fidl::BytePart response_buffer{{ end }})
{{- end }}

{{- define "SyncRequestInPlaceMethodDefinition" }}
{{- $interface_name := .LLProps.InterfaceName }}
{{ if .Response }}::fidl::DecodeResult<{{ $interface_name }}::{{ .Name }}Response>{{ else }}zx_status_t{{ end }} {{ $interface_name }}::SyncClient::{{ template "SyncRequestInPlaceMethodSignature" . }} {
  {{- if not .Request }}
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof({{ .Name }}Request)] = {};
  constexpr uint32_t _write_num_bytes = sizeof({{ .Name }}Request);
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes), _write_num_bytes);
  ::fidl::DecodedMessage<{{ .Name }}Request> params(std::move(_request_buffer));
  {{- end }}
  params.message()->_hdr = {};
  params.message()->_hdr.ordinal = {{ .OrdinalName }};
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
  {{- if .Response }}
    return ::fidl::DecodeResult<{{ $interface_name }}::{{ .Name }}Response>(
      _encode_request_result.status,
      _encode_request_result.error,
      ::fidl::DecodedMessage<{{ $interface_name }}::{{ .Name }}Response>());
  {{- else }}
    return _encode_request_result.status;
  {{- end }}
  }
  {{- if .HasResponse }}
    {{- if not .Response }}
  constexpr uint32_t _kReadAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response>();
  FIDL_ALIGNDECL uint8_t _read_bytes[_kReadAllocSize];
  ::fidl::BytePart response_buffer(_read_bytes, sizeof(_read_bytes));
    {{- end }}
  auto _call_result = ::fidl::Call<{{ .Name }}Request, {{ .Name }}Response>(
    this->channel_, std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    {{- if .Response }}
    return ::fidl::DecodeResult<{{ $interface_name }}::{{ .Name }}Response>(
      _call_result.status,
      _call_result.error,
      ::fidl::DecodedMessage<{{ $interface_name }}::{{ .Name }}Response>());
    {{- else }}
    return _call_result.status;
    {{- end }}
  }
    {{- if .Response }}
  return ::fidl::Decode(std::move(_call_result.message));
    {{- else }}
  auto _decode_result = ::fidl::Decode(std::move(_call_result.message));
  return _decode_result.status;
    {{- end }}
  {{- else }}
  return ::fidl::Write(this->channel_, std::move(_encode_request_result.message));
  {{- end }}
}
{{- end }}
`
