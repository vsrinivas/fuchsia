// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncRequestManagedTmpl = `
{{- define "ParamsNoTypedChannels" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.WireNoTypedChannels }} {{ $param.Name }}
  {{- end -}}
{{- end }}

{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Wire }} {{ $param.Name }}
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
::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}> _client_end
{{- if .Request }}, {{ end }}
{{- template "Params" .Request }}
{{- end }}

{{- define "SyncRequestManagedMethodDefinition" }}
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}::{{ .Name }}(
    ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}> _client
    {{- template "CommaMessagePrototype" .Request }})
    {{- if gt .ResponseReceivedMaxSize 512 -}}
  : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{ template "ResponseReceivedSize" . }}>>())
    {{- end }}
   {
  {{ .Name }}Request::OwnedEncodedMessage _request(zx_txid_t(0)
    {{- template "CommaPassthroughMessageParams" .Request -}});
  {{- if .HasResponse }}
  _request.GetOutgoingMessage().Call<{{ .Name }}Response>(
      _client,
      {{- template "ResponseReceivedByteAccess" . }},
      {{ template "ResponseReceivedSize" . }});
  {{- else }}
  _request.GetOutgoingMessage().Write(_client);
  {{- end }}
  status_ = _request.status();
  error_ = _request.error();
}
  {{- if .HasResponse }}

{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}::{{ .Name }}(
    ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}> _client
    {{- template "CommaMessagePrototype" .Request -}}
    , zx_time_t _deadline)
    {{- if gt .ResponseReceivedMaxSize 512 -}}
  : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{ template "ResponseReceivedSize" . }}>>())
    {{- end }}
   {
  {{ .Name }}Request::OwnedEncodedMessage _request(zx_txid_t(0)
    {{- template "CommaPassthroughMessageParams" .Request -}});
  _request.GetOutgoingMessage().Call<{{ .Name }}Response>(
      _client,
      {{- template "ResponseReceivedByteAccess" . }},
      {{ template "ResponseReceivedSize" . }},
      _deadline);
  status_ = _request.status();
  error_ = _request.error();
}
  {{- end }}
{{- end }}
`
