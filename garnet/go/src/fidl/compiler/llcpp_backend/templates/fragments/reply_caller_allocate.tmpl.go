// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ReplyCallerAllocate = `
{{- define "ReplyCallerAllocateMethodSignature" -}}
Reply(::fidl::BytePart _buffer {{- if .Response }}, {{ end }}{{ template "Params" .Response }})
{{- end }}

{{- define "ReplyCallerAllocateMethodDefinition" }}
void {{ .LLProps.InterfaceName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCallerAllocateMethodSignature" . }} {
  if (_buffer.capacity() < {{ .Name }}Response::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
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
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  CompleterBase::SendReply(std::move(_linearize_result.message));
  {{- else }}
  _buffer.set_actual(sizeof({{ .Name }}Response));
  CompleterBase::SendReply(::fidl::DecodedMessage<{{ .Name }}Response>(std::move(_buffer)));
  {{- end }}
}
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodSignature" -}}
ReplySuccess(::fidl::BytePart _buffer {{- if .Result.ValueMembers }}, {{ end }}{{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodDefinition" }}
void {{ .LLProps.InterfaceName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCallerAllocateResultSuccessMethodSignature" . }} {
  {{ .Result.ValueStructDecl }} response;
  {{- range .Result.ValueMembers }}
  response.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  Reply(std::move(_buffer), {{ .Result.ResultDecl }}::WithResponse(&response));
}
{{- end }}
`
