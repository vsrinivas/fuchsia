// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncRequestManagedTmpl = `
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
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}::{{ .Name }}(
  zx_handle_t _client {{- template "CommaMessagePrototype" .Request }})
    {{- if gt .ResponseReceivedMaxSize 512 -}}
  : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{ template "ResponseReceivedSize" . }}>>())
    {{- end }}
   {
  {{ .Name }}OwnedRequest _request(0
    {{- template "CommaPassthroughMessageParams" .Request -}});
  {{- if .HasResponse }}
  _request.GetFidlMessage().Call({{ .Name }}Response::Type, _client,
                                 {{- template "ResponseReceivedByteAccess" . }},
                                 {{ template "ResponseReceivedSize" . }});
  {{- else }}
  _request.GetFidlMessage().Write(_client);
  {{- end }}
  status_ = _request.status();
  error_ = _request.error();
}

{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::SyncClient::{{ .Name }}(
  {{- template "SyncRequestManagedMethodArguments" . }}) {
    return ResultOf::{{ .Name }}(this->channel().get()
    {{- template "CommaPassthroughMessageParams" .Request -}}
  );
}
{{- end }}

{{- define "StaticCallSyncRequestManagedMethodDefinition" }}
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }} {{ .LLProps.ProtocolName }}::Call::{{ .Name }}(
  {{- template "StaticCallSyncRequestManagedMethodArguments" . }}) {
  return ResultOf::{{ .Name }}(_client_end->get()
    {{- template "CommaPassthroughMessageParams" .Request -}}
  );
}
{{- end }}
`
