// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyManagedTmpl = `
{{- define "ReplyManagedMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::Reply({{ RenderCalleeParams .ResponseArgs }}) {
  ::fidl::OwnedEncodedMessage<{{ .WireResponse }}> _response{
    {{- RenderForwardParams .ResponseArgs -}}
  };
  return {{ .WireCompleterBase }}::SendReply(&_response.GetOutgoingMessage());
}
{{- EndifFuchsia -}}
{{- end }}


{{- define "ReplyManagedResultSuccessMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::ReplySuccess({{ RenderCalleeParams .Result.ValueMembers }}) {
  {{ .Result.ValueStructDecl }} _response;
  {{- range .Result.ValueMembers }}
  _response.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply({{ .Result.ResultDecl }}::WithResponse(
      ::fidl::ObjectView<{{ .Result.ValueStructDecl }}>::FromExternal(&_response)));
}
{{- EndifFuchsia -}}
{{- end }}

{{- define "ReplyManagedResultErrorMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::ReplyError({{ .Result.ErrorDecl }} error) {
  return Reply({{ .Result.ResultDecl }}::WithErr(
      ::fidl::ObjectView<{{ .Result.ErrorDecl }}>::FromExternal(&error)));
}
{{- EndifFuchsia -}}
{{- end }}
`
