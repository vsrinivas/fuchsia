// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ReplyCFlavor = `
{{- define "ReplyCFlavorMethodSignature" -}}
Reply({{ template "Params" .Response }})
{{- end }}

{{- define "ReplyCFlavorMethodDefinition" }}
void {{ .LLProps.InterfaceName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorMethodSignature" . }} {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response>();

  {{- if .LLProps.StackAllocResponse }}
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
  _response._hdr.ordinal = {{ .OrdinalName }};
  {{- template "FillResponseStructMembers" .Response -}}

  {{- if .LLProps.LinearizeResponse }}
  auto _linearize_result = ::fidl::Linearize(&_response, ::fidl::BytePart(_write_bytes,
                                                                          _kWriteAllocSize));
  if (_linearize_result.status != ZX_OK) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  CompleterBase::SendReply(std::move(_linearize_result.message));
  {{- else }}
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof({{ .Name }}Response));
  CompleterBase::SendReply(::fidl::DecodedMessage<{{ .Name }}Response>(std::move(_response_bytes)));
  {{- end }}
}
{{- end }}
`
