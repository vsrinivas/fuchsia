// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyCallerAllocateTmpl = `
{{- define "ReplyCallerAllocateMethodDefinition" }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .WireCompleterBase.NoLeading }}::Reply(
      {{- RenderCalleeParams "::fidl::BufferSpan _buffer" .ResponseArgs }}) {
  {{ .WireResponse }}::UnownedEncodedMessage _response(
      {{ RenderForwardParams "_buffer.data" "_buffer.capacity" .ResponseArgs }});
  return CompleterBase::SendReply(&_response.GetOutgoingMessage());
}
{{- EndifFuchsia -}}
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodDefinition" }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .WireCompleterBase.NoLeading }}::ReplySuccess(
      {{- RenderCalleeParams "::fidl::BufferSpan _buffer" .Result.ValueMembers }}) {
  {{ .Result.ValueStructDecl }} response;
  {{- range .Result.ValueMembers }}
  response.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply(
      std::move(_buffer),
      {{ .Result.ResultDecl }}::WithResponse(
          ::fidl::ObjectView<{{ .Result.ValueStructDecl }}>::FromExternal(&response)));
}
{{- EndifFuchsia -}}
{{- end }}
`
