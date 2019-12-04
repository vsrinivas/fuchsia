// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SyncRequestManaged = `
{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.LLDecl }} {{ $param.Name }}
  {{- end -}}
{{- end }}

{{- define "SyncClientMoveParams" }}
  {{- range $index, $param := . }}
    {{- if $index }}, {{ end -}} std::move({{ $param.Name }})
  {{- end }}
{{- end }}

{{- define "SyncRequestManagedMethodArguments" -}}
{{ template "Params" .Request }}
{{- end }}

{{- define "StaticCallSyncRequestManagedMethodArguments" -}}
::zx::unowned_channel _client_end {{- if .Request }}, {{ end }}{{ template "Params" .Request }}
{{- end }}

{{- define "SyncRequestManagedMethodDefinition" }}
{{ if .HasResponse -}} template <> {{- end }}
{{ .LLProps.InterfaceName }}::ResultOf::{{ .Name }}_Impl {{- if .HasResponse -}} <{{ .LLProps.InterfaceName }}::{{ .Name }}Response> {{- end }}::{{ .Name }}_Impl(
  {{- template "StaticCallSyncRequestManagedMethodArguments" . }}) {
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
  {{- template "SyncRequestManagedMethodArguments" . }}) {
    return ResultOf::{{ .Name }}(::zx::unowned_channel(this->channel_)
    {{- if .Request }}, {{ end }}
    {{- template "SyncClientMoveParams" .Request -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestManagedMethodDefinition" }}
{{ .LLProps.InterfaceName }}::ResultOf::{{ .Name }} {{ .LLProps.InterfaceName }}::Call::{{ .Name }}(
  {{- template "StaticCallSyncRequestManagedMethodArguments" . }}) {
  return ResultOf::{{ .Name }}(std::move(_client_end)
    {{- if .Request }}, {{ end }}
    {{- template "SyncClientMoveParams" .Request }});
}
{{- end }}
`
