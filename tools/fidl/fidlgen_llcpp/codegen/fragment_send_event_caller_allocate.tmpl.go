// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSendEventCallerAllocateTmpl = `
{{- define "SendEventCallerAllocateMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _chan, ::fidl::BytePart _buffer, {{ template "Params" .Response }})
{{- end }}

{{- define "SendEventCallerAllocateMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::{{ template "SendEventCallerAllocateMethodSignature" . }} {
  if (_buffer.capacity() < {{ .Name }}Response::PrimarySize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  {{- if .LLProps.LinearizeResponse }}
  {{ .Name }}Response _response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  {{- else }}
  new (_buffer.data()) {{ .Name }}Response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  {{- end }}

  {{- if .LLProps.LinearizeResponse }}
  auto _encode_result = ::fidl::LinearizeAndEncode<{{ .Name }}Response>(&_response, std::move(_buffer));
  if (_encode_result.status != ZX_OK) {
    return _encode_result.status;
  }
  return ::fidl::Write(::zx::unowned_channel(_chan), std::move(_encode_result.message));
  {{- else }}
  _buffer.set_actual(sizeof({{ .Name }}Response));
  return ::fidl::Write(::zx::unowned_channel(_chan), ::fidl::DecodedMessage<{{ .Name }}Response>(std::move(_buffer)));
  {{- end }}
}
{{- end }}
`
