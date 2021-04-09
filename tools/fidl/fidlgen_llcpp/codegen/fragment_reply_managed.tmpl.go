// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyManagedTmpl = `
{{- define "ReplyManagedMethodSignature" -}}
Reply({{ .ResponseArgs | CalleeParams }})
{{- end }}

{{- define "ReplyManagedMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::
    {{- template "ReplyManagedMethodSignature" . }} {
  ::fidl::OwnedEncodedMessage<{{ .WireResponse }}> _response{
    {{- .ResponseArgs | ForwardParams -}}
  };
  return {{ .WireCompleterBase }}::SendReply(&_response.GetOutgoingMessage());
}
{{- EndifFuchsia -}}
{{- end }}

{{- define "ReplyManagedResultSuccessMethodSignature" -}}
ReplySuccess({{ .Result.ValueMembers | CalleeParams }})
{{- end }}

{{- define "ReplyManagedResultSuccessMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::
    {{- template "ReplyManagedResultSuccessMethodSignature" . }} {
  {{ .Result.ValueStructDecl }} _response;
  {{- range .Result.ValueMembers }}
  _response.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply({{ .Result.ResultDecl }}::WithResponse(
      ::fidl::ObjectView<{{ .Result.ValueStructDecl }}>::FromExternal(&_response)));
}
{{- EndifFuchsia -}}
{{- end }}

{{- define "ReplyManagedResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyManagedResultErrorMethodDefinition" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::
    {{- template "ReplyManagedResultErrorMethodSignature" . }} {
  return Reply({{ .Result.ResultDecl }}::WithErr(
      ::fidl::ObjectView<{{ .Result.ErrorDecl }}>::FromExternal(&error)));
}
{{- EndifFuchsia -}}
{{- end }}
`
