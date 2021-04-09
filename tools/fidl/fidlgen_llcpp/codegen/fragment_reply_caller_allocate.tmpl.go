// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyCallerAllocateTmpl = `
{{- define "ReplyCallerAllocateMethodSignature" -}}
Reply(::fidl::BufferSpan _buffer {{- if .ResponseArgs }}, {{ end }}
      {{ .ResponseArgs | CalleeParams }})
{{- end }}

{{- define "ReplyCallerAllocateMethodDefinition" }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .WireCompleterBase.NoLeading }}::
{{- template "ReplyCallerAllocateMethodSignature" . }} {
  {{ .WireResponse }}::UnownedEncodedMessage _response(_buffer.data, _buffer.capacity
  {{- .ResponseArgs | ForwardCommaParams -}}
  );
  return CompleterBase::SendReply(&_response.GetOutgoingMessage());
}
{{- EndifFuchsia -}}
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodSignature" -}}
ReplySuccess(::fidl::BufferSpan _buffer {{- if .Result.ValueMembers }}, {{ end }}
             {{ .Result.ValueMembers | CalleeParams }})
{{- end }}

{{- define "ReplyCallerAllocateResultSuccessMethodDefinition" }}
{{- IfdefFuchsia -}}
::fidl::Result {{ .WireCompleterBase.NoLeading }}::
{{- template "ReplyCallerAllocateResultSuccessMethodSignature" . }} {
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
