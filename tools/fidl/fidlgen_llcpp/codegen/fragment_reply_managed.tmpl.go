// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyManagedTmpl = `
{{- define "ReplyManagedMethodSignature" -}}
Reply({{ template "Params" .Response }})
{{- end }}

{{- define "ReplyManagedMethodDefinition" }}
::fidl::Result
{{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::
    {{- template "ReplyManagedMethodSignature" . }} {
  {{ .Name }}Response::OwnedOutgoingMessage _response{
      {{- template "PassthroughMessageParams" .Response -}}
  };
  return CompleterBase::SendReply(&_response.GetOutgoingMessage());
}
{{- end }}

{{- define "ReplyManagedResultSuccessMethodSignature" -}}
ReplySuccess({{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyManagedResultSuccessMethodDefinition" }}
::fidl::Result
{{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::
    {{- template "ReplyManagedResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> _response;
  {{- range .Result.ValueMembers }}
  _response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply({{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&_response)));
}
{{- end }}

{{- define "ReplyManagedResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyManagedResultErrorMethodDefinition" }}
::fidl::Result
{{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::
    {{- template "ReplyManagedResultErrorMethodSignature" . }} {
  return Reply({{ .Result.ResultDecl }}::WithErr(::fidl::unowned_ptr(&error)));
}
{{- end }}
`
