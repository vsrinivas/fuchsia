// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SendEventCallerAllocate = `
{{- define "SendEventCallerAllocateMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _chan, ::fidl::BytePart _buffer, {{ template "Params" .Response }})
{{- end }}

{{- define "SendEventCallerAllocateMethodDefinition" }}
zx_status_t {{ .LLProps.InterfaceName }}::{{ template "SendEventCallerAllocateMethodSignature" . }} {
  if (_buffer.capacity() < {{ .Name }}Response::PrimarySize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  {{- if .LLProps.LinearizeResponse }}
  {{ .Name }}Response _response = {};
  {{- else }}
  auto& _response = *reinterpret_cast<{{ .Name }}Response*>(_buffer.data());
  {{- end }}
  {{- template "SetTransactionHeaderForResponse" . }}
  {{- template "FillResponseStructMembers" .Response -}}

  {{- if .LLProps.LinearizeResponse }}
  auto _linearize_result = ::fidl::Linearize(&_response, std::move(_buffer));
  if (_linearize_result.status != ZX_OK) {
    return _linearize_result.status;
  }
  return ::fidl::Write(::zx::unowned_channel(_chan), std::move(_linearize_result.message));
  {{- else }}
  _buffer.set_actual(sizeof({{ .Name }}Response));
  return ::fidl::Write(::zx::unowned_channel(_chan), ::fidl::DecodedMessage<{{ .Name }}Response>(std::move(_buffer)));
  {{- end }}
}
{{- end }}
`
