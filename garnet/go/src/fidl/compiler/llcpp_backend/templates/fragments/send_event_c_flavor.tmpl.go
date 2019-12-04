// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SendEventCFlavor = `
{{- define "SendEventCFlavorMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _chan {{- if .Response }}, {{ end }}{{ template "Params" .Response }})
{{- end }}

{{- define "SendEventCFlavorMethodDefinition" }}
zx_status_t {{ .LLProps.InterfaceName }}::{{ template "SendEventCFlavorMethodSignature" . }} {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kSending>();

  {{- if .LLProps.ClientContext.StackAllocResponse }}
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] {{- if not .LLProps.LinearizeResponse }} = {} {{- end }};
  {{- else }}
  std::unique_ptr<uint8_t[]> _write_bytes_unique_ptr(new uint8_t[_kWriteAllocSize]);
  uint8_t* _write_bytes = _write_bytes_unique_ptr.get();
  {{- end }}

  {{- if .LLProps.LinearizeResponse }}
  {{ .Name }}Response _response = {};
  {{- else }}
  auto& _response = *reinterpret_cast<{{ .Name }}Response*>(_write_bytes);
  {{- end }}
  {{- template "SetTransactionHeaderForResponse" . }}
  {{- template "FillResponseStructMembers" .Response -}}

  {{- if .LLProps.LinearizeResponse }}
  auto _linearize_result = ::fidl::Linearize(&_response, ::fidl::BytePart(_write_bytes,
                                                                          _kWriteAllocSize));
  if (_linearize_result.status != ZX_OK) {
    return _linearize_result.status;
  }
  return ::fidl::Write(::zx::unowned_channel(_chan), std::move(_linearize_result.message));
  {{- else }}
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof({{ .Name }}Response));
  return ::fidl::Write(::zx::unowned_channel(_chan), ::fidl::DecodedMessage<{{ .Name }}Response>(std::move(_response_bytes)));
  {{- end }}
}
{{- end }}
`
