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

{{- define "ReplyCFlavorResultSuccessMethodSignature" -}}
ReplySuccess({{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyCFlavorResultSuccessMethodDefinition" }}
void {{ .LLProps.InterfaceName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultSuccessMethodSignature" . }} {
  {{ .Result.ValueStructDecl }} response;
  {{- range .Result.ValueMembers }}
  response.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  Reply({{ .Result.ResultDecl }}::WithResponse(&response));
}
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodDefinition" }}
void {{ .LLProps.InterfaceName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultErrorMethodSignature" . }} {
  Reply({{ .Result.ResultDecl }}::WithErr(&error));
}
{{- end }}
`
