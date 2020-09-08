// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncRequestCallerAllocateTmpl = `
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
{{ .LLProps.ProtocolName }}::UnownedResultOf::{{ .Name }}::{{ .Name }}(
  zx_handle_t _client
  {{- if .Request -}}
  , uint8_t* _request_bytes, uint32_t _request_byte_capacity
  {{- end -}}
  {{- template "CommaMessagePrototype" .Request }}
  {{- if .HasResponse }}
  , uint8_t* _response_bytes, uint32_t _response_byte_capacity)
    : bytes_(_response_bytes) {
  {{- else }}
  ) {
  {{- end }}
  {{- if .Request -}}
  {{ .Name }}UnownedRequest _request(_request_bytes, _request_byte_capacity, 0
  {{- else -}}
  {{ .Name }}OwnedRequest _request(0
  {{- end -}}
    {{- template "CommaPassthroughMessageParams" .Request -}});
  {{- if .HasResponse }}
  _request.GetFidlMessage().Call({{ .Name }}Response::Type, _client, _response_bytes, _response_byte_capacity);
  {{- else }}
  _request.GetFidlMessage().Write(_client);
  {{- end }}
  status_ = _request.status();
  error_ = _request.error();
}
{{- end }}
`
