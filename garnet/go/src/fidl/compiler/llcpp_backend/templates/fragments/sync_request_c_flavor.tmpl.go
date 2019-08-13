// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SyncRequestCFlavor = `
{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.LLDecl }} {{ $param.Name }}
  {{- end -}}
{{- end }}

{{- define "OutParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.LLDecl }}* out_{{ $param.Name }}
  {{- end -}}
{{- end }}

{{- define "SyncClientMoveParams" }}
  {{- range $index, $param := . }}
    {{- if $index }}, {{ end -}} std::move({{ $param.Name }})
  {{- end }}
{{- end }}

{{- define "SyncRequestCFlavorMethodSignature" -}}
  {{- if .Response -}}
{{ .Name }}_Deprecated({{ template "Params" .Request }}{{ if .Request }}, {{ end }}{{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}_Deprecated({{ template "Params" .Request }})
  {{- end -}}
{{- end }}

{{- define "StaticCallSyncRequestCFlavorMethodSignature" -}}
  {{- if .Response -}}
{{ .Name }}_Deprecated(zx::unowned_channel _client_end, {{ template "Params" .Request }}{{ if .Request }}, {{ end }}{{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}_Deprecated(zx::unowned_channel _client_end {{- if .Request }}, {{ end }}{{ template "Params" .Request }})
  {{- end -}}
{{- end }}

{{- define "SyncRequestCFlavorMethodArgumentsNew" -}}
{{ template "Params" .Request }}
{{- end }}

{{- define "StaticCallSyncRequestCFlavorMethodArgumentsNew" -}}
zx::unowned_channel _client_end {{- if .Request }}, {{ end }}{{ template "Params" .Request }}
{{- end }}

{{- define "SyncRequestCFlavorMethodDefinition" }}
zx_status_t {{ .LLProps.InterfaceName }}::SyncClient::{{ template "SyncRequestCFlavorMethodSignature" . }} {
  return {{ .LLProps.InterfaceName }}::Call::{{ .Name }}_Deprecated(zx::unowned_channel(this->channel_)
    {{- if or .Request .Response }}, {{ end }}
    {{- template "SyncClientMoveParams" .Request }}
    {{- if and .Request .Response }}, {{ end }}
    {{- range $index, $param := .Response -}}
      {{- if $index }}, {{ end -}} out_{{ $param.Name }}
    {{- end -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestCFlavorMethodDefinition" }}
zx_status_t {{ .LLProps.InterfaceName }}::Call::{{ template "StaticCallSyncRequestCFlavorMethodSignature" . }} {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Request, ::fidl::MessageDirection::kSending>();

  {{- if .LLProps.ClientContext.StackAllocRequest }}
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] {{- if not .LLProps.LinearizeRequest }} = {} {{- end }};
  {{- else }}
  std::unique_ptr<uint8_t[]> _write_bytes_unique_ptr(new uint8_t[_kWriteAllocSize]);
  uint8_t* _write_bytes = _write_bytes_unique_ptr.get();
  {{- end }}

  {{- if .LLProps.LinearizeRequest }}
  {{ .Name }}Request _request = {};
  {{- else }}
  auto& _request = *reinterpret_cast<{{ .Name }}Request*>(_write_bytes);
  {{- end }}
  _request._hdr.ordinal = {{ .Ordinals.Write.Name }};
  {{- template "FillRequestStructMembers" .Request -}}

  {{- if .LLProps.LinearizeRequest }}
  auto _linearize_result = ::fidl::Linearize(&_request, ::fidl::BytePart(_write_bytes,
                                                                         _kWriteAllocSize));
  if (_linearize_result.status != ZX_OK) {
    return _linearize_result.status;
  }
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request = std::move(_linearize_result.message);
  {{- else }}
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof({{ .Name }}Request));
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request(std::move(_request_bytes));
  {{- end }}
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return _encode_request_result.status;
  }

  {{- if .HasResponse }}

  {{- /* Has response */}}
  constexpr uint32_t _kReadAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving>();
  {{- if .LLProps.ClientContext.StackAllocResponse }}
  FIDL_ALIGNDECL uint8_t _read_bytes[_kReadAllocSize];
  {{- else }}
  std::unique_ptr<uint8_t[]> _read_bytes_unique_ptr(new uint8_t[_kReadAllocSize]);
  uint8_t* _read_bytes = _read_bytes_unique_ptr.get();
  {{- end }}
  ::fidl::BytePart _response_bytes(_read_bytes, _kReadAllocSize);
  auto _call_result = ::fidl::Call<{{ .Name }}Request, {{ .Name }}Response>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(_response_bytes));
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

  {{- /* Does not have response */}}
  return ::fidl::Write(std::move(_client_end), std::move(_encode_request_result.message));
  {{- end }}
}
{{- end }}

{{- define "SyncRequestCFlavorMethodDefinitionNew" }}
{{ if .HasResponse -}} template <> {{- end }}
{{ .LLProps.InterfaceName }}::ResultOf::{{ .Name }}_Impl {{- if .HasResponse -}} <{{ .LLProps.InterfaceName }}::{{ .Name }}Response> {{- end }}::{{ .Name }}_Impl(
  {{- template "StaticCallSyncRequestCFlavorMethodArgumentsNew" . }}) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Request, ::fidl::MessageDirection::kSending>();

  {{- if .LLProps.ClientContext.StackAllocRequest }}
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  {{- else }}
  std::unique_ptr _write_bytes_boxed = std::make_unique<::fidl::internal::AlignedBuffer<_kWriteAllocSize>>();
  auto& _write_bytes_array = *_write_bytes_boxed;
  {{- end }}

  {{- if .LLProps.LinearizeRequest }}
  {{ .Name }}Request _request = {};
  {{- else }}
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, {{ .Name }}Request::PrimarySize);
    {{- if .Request }}
  auto& _request = *reinterpret_cast<{{ .Name }}Request*>(_write_bytes);
    {{- end }}
  {{- end }}
  {{- template "FillRequestStructMembers" .Request -}}

  {{- if .LLProps.LinearizeRequest }}
  auto _linearize_result = ::fidl::Linearize(&_request, _write_bytes_array.view());
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request = std::move(_linearize_result.message);
  {{- else }}
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof({{ .Name }}Request));
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request(std::move(_request_bytes));
  {{- end }}

  {{- if .HasResponse }}
  Super::SetResult(
      {{ .LLProps.InterfaceName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_decoded_request){{ end -}}
      , Super::response_buffer()));
  {{- else }}
  Super::operator=(
      {{ .LLProps.InterfaceName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_decoded_request){{ end -}}
  ));
  {{- end }}
}

{{ .LLProps.InterfaceName }}::ResultOf::{{ .Name }} {{ .LLProps.InterfaceName }}::SyncClient::{{ .Name }}(
  {{- template "SyncRequestCFlavorMethodArgumentsNew" . }}) {
  return ResultOf::{{ .Name }}(zx::unowned_channel(this->channel_)
    {{- if .Request }}, {{ end }}
    {{- template "SyncClientMoveParams" .Request -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestCFlavorMethodDefinitionNew" }}
{{ .LLProps.InterfaceName }}::ResultOf::{{ .Name }} {{ .LLProps.InterfaceName }}::Call::{{ .Name }}(
  {{- template "StaticCallSyncRequestCFlavorMethodArgumentsNew" . }}) {
  return ResultOf::{{ .Name }}(std::move(_client_end)
    {{- if .Request }}, {{ end }}
    {{- template "SyncClientMoveParams" .Request }});
}
{{- end }}
`
