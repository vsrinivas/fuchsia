// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ReplyCFlavor = `
{{- define "ReplyCFlavorMethodSignature" -}}
Reply({{ template "Params" .Response }})
{{- end }}

{{- define "ReplyCFlavorMethodDefinition" }}
void {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorMethodSignature" . }} {
  {{- if .LLProps.LinearizeResponse }}
  {{/* tracking_ptr destructors will be called when _response goes out of scope */}}
  {{ .Name }}Response _response = {};
  {{- else }}
  {{/* tracking_ptrs won't free allocated memory because destructors aren't called.
  This is ok because there are no tracking_ptrs, since LinearizeResponse is true when
  there are pointers in the object. */}}
  // Destructors can't be called because it will lead to handle double close
  // (here and in fidl::Encode).
  FIDL_ALIGNDECL uint8_t _response_buffer[sizeof({{ .Name }}Response)]{};
  auto& _response = *reinterpret_cast<{{ .Name }}Response*>(_response_buffer);
  {{- end }}
  {{- template "SetTransactionHeaderForResponse" . }}
  {{- template "FillResponseStructMembers" .Response -}}

  auto _encoded = ::fidl::internal::LinearizedAndEncoded<{{ .Name }}Response>(&_response);
  auto& _encode_result = _encoded.result();
  if (_encode_result.status != ZX_OK) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  CompleterBase::SendReply(std::move(_encode_result.message));
}
{{- end }}

{{- define "ReplyCFlavorResultSuccessMethodSignature" -}}
ReplySuccess({{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyCFlavorResultSuccessMethodDefinition" }}
void {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> response;
  {{- range .Result.ValueMembers }}
  response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  Reply({{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&response)));
}
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodDefinition" }}
void {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultErrorMethodSignature" . }} {
  Reply({{ .Result.ResultDecl }}::WithErr(::fidl::unowned_ptr(&error)));
}
{{- end }}
`
