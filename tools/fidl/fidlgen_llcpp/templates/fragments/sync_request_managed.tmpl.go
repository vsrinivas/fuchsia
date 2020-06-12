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
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}_Impl {{- if .HasResponse -}} <{{ .LLProps.ProtocolName }}::{{ .Name }}Response> {{- end }}::{{ .Name }}_Impl(
  {{- template "StaticCallSyncRequestManagedMethodArguments" . }}) {

  {{- if .LLProps.LinearizeRequest }}
  {{/* tracking_ptr destructors will be called when _response goes out of scope */}}
  {{ .Name }}Request _request = {};
  {{- else }}
  {{/* tracking_ptrs won't free allocated memory because destructors aren't called.
  This is ok because there are no tracking_ptrs, since LinearizeResponse is true when
  there are pointers in the object. */}}
  // Destructors can't be called because it will lead to handle double close
  // (here and in fidl::Encode).
  FIDL_ALIGNDECL uint8_t _request_buffer[sizeof({{ .Name }}Request)]{};
  auto& _request = *reinterpret_cast<{{ .Name }}Request*>(_request_buffer);
  {{- end }}
  {{- template "FillRequestStructMembers" .Request -}}

  auto _encoded = ::fidl::internal::LinearizedAndEncoded<{{ .Name }}Request>(&_request);
  auto& _encode_result = _encoded.result();
  if (_encode_result.status != ZX_OK) {
    Super::SetFailure(std::move(_encode_result));
    return;
  }
  ::fidl::EncodedMessage<{{ .Name }}Request> _encoded_request = std::move(_encode_result.message);

  {{- if .HasResponse }}
  Super::SetResult(
      {{ .LLProps.ProtocolName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_encoded_request){{ end -}}
      , Super::response_buffer()));
  {{- else }}
  Super::operator=(
      {{ .LLProps.ProtocolName }}::InPlace::{{ .Name }}(std::move(_client_end)
      {{- if .Request }}, std::move(_encoded_request){{ end -}}
  ));
  {{- end }}
}

{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::SyncClient::{{ .Name }}(
  {{- template "SyncRequestManagedMethodArguments" . }}) {
    return ResultOf::{{ .Name }}(::zx::unowned_channel(this->channel_)
    {{- if .Request }}, {{ end }}
    {{- template "SyncClientMoveParams" .Request -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestManagedMethodDefinition" }}
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::Call::{{ .Name }}(
  {{- template "StaticCallSyncRequestManagedMethodArguments" . }}) {
  return ResultOf::{{ .Name }}(std::move(_client_end)
    {{- if .Request }}, {{ end }}
    {{- template "SyncClientMoveParams" .Request }});
}
{{- end }}
`
