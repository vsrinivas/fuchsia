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
{{ .Name }}({{ template "CallerBufferParams" .Request }}{{ if .Request }}, {{ end }}::fidl::BytePart _response_buffer, {{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}({{ template "CallerBufferParams" .Request }})
  {{- end -}}
{{- end }}

{{- define "SyncRequestCallerAllocateMethodDefinition" }}
zx_status_t {{ .LLProps.InterfaceName }}::SyncClient::{{ template "SyncRequestCallerAllocateMethodSignature" . }} {
  {{- if not .Request }}
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof({{ .Name }}Request)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  {{- end }}
  {{- if .LLProps.NeedToLinearize }}
  {{ .Name }}Request _request = {};
  {{- else }}
  auto& _request = *reinterpret_cast<{{ .Name }}Request*>(_request_buffer.data());
  {{- end }}
  _request._hdr.ordinal = {{ .OrdinalName }};
  {{- template "FillRequestStructMembers" .Request -}}

  {{- if .LLProps.NeedToLinearize }}
  auto _linearize_result = ::fidl::Linearize(&_request, std::move(_request_buffer));
  if (_linearize_result.status != ZX_OK) {
    return _linearize_result.status;
  }
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request = std::move(_linearize_result.message);
  {{- else }}
  _request_buffer.set_actual(sizeof({{ .Name }}Request));
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request(std::move(_request_buffer));
  {{- end }}
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return _encode_request_result.status;
  }
  {{- if .HasResponse }}
    {{- if not .Response }}
  constexpr uint32_t _kReadAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response>();
  FIDL_ALIGNDECL uint8_t _read_bytes[_kReadAllocSize];
  ::fidl::BytePart _response_buffer(_read_bytes, sizeof(_read_bytes));
    {{- end }}
  auto _call_result = ::fidl::Call<{{ .Name }}Request, {{ .Name }}Response>(
    this->channel_, std::move(_encode_request_result.message), std::move(_response_buffer));
  if (_call_result.status != ZX_OK) {
    return _call_result.status;
  }
  auto _decode_result = ::fidl::Decode(std::move(_call_result.message));
  if (_decode_result.status != ZX_OK) {
    return _decode_result.status;
  }
    {{- if .Response }}
  auto& _response = *_decode_result.message.message();
    {{- end }}
    {{- template "ReturnResponseStructMembers" .Response }}
  return ZX_OK;
  {{- else }}
  const auto& _oneway = _encode_request_result.message;
  return this->channel_.write(0,
                              _oneway.bytes().data(), _oneway.bytes().actual(),
                              _oneway.handles().data(), _oneway.handles().actual());
  {{- end }}
}
{{- end }}
`
