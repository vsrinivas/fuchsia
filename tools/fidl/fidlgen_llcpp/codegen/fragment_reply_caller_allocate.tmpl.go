// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyCallerAllocateTmpl = `
{{- define "ReplyCallerAllocateMethodSignature" -}}
Reply(::fidl::BytePart _buffer {{- if .Response }}, {{ end }}{{ template "Params" .Response }})
{{- end }}

{{- define "ReplyCallerAllocateMethodDefinition" }}
::fidl::Result {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCallerAllocateMethodSignature" . }} {
  {{ .Name }}UnownedResponse _response(_buffer.data(), _buffer.capacity()
  {{- template "CommaPassthroughMessageParams" .Response -}}
  );
  return CompleterBase::SendReply(&_response.GetFidlMessage());
}
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodSignature" -}}
ReplySuccess(::fidl::BytePart _buffer {{- if .Result.ValueMembers }}, {{ end }}{{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodDefinition" }}
::fidl::Result {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCallerAllocateResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> response;
  {{- range .Result.ValueMembers }}
  response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply(std::move(_buffer), {{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&response)));
}
{{- end }}
`
