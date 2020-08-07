// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ReplyCallerAllocate = `
{{- define "ReplyCallerAllocateMethodSignature" -}}
Reply(::fidl::BytePart _buffer {{- if .Response }}, {{ end }}{{ template "Params" .Response }})
{{- end }}

{{- define "ReplyCallerAllocateMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCallerAllocateMethodSignature" . }} {
  if (_buffer.capacity() < {{ .Name }}Response::PrimarySize) {
    CompleterBase::InternalError({::fidl::UnbindInfo::kEncodeError, ZX_ERR_BUFFER_TOO_SMALL});
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
    CompleterBase::InternalError({::fidl::UnbindInfo::kEncodeError, _encode_result.status});
    return _encode_result.status;
  }
  return CompleterBase::SendReply(std::move(_encode_result.message));
  {{- else }}
  _buffer.set_actual(sizeof({{ .Name }}Response));
  return CompleterBase::SendReply(::fidl::DecodedMessage<{{ .Name }}Response>(std::move(_buffer)));
  {{- end }}
}
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodSignature" -}}
ReplySuccess(::fidl::BytePart _buffer {{- if .Result.ValueMembers }}, {{ end }}{{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCallerAllocateResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> response;
  {{- range .Result.ValueMembers }}
  response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply(std::move(_buffer), {{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&response)));
}
{{- end }}
`
