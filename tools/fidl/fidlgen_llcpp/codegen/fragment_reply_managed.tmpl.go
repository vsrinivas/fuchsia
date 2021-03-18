// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyManagedTmpl = `
{{- define "ReplyManagedMethodSignature" -}}
Reply({{ .ResponseArgs | Params }})
{{- end }}

{{- define "ReplyManagedMethodDefinition" }}
#ifdef __Fuchsia__
::fidl::Result
{{ .LLProps.ProtocolName.Name }}::Interface::{{ .Name }}CompleterBase::
    {{- template "ReplyManagedMethodSignature" . }} {
  ::fidl::OwnedEncodedMessage<{{ .Name }}Response> _response{
    {{- .ResponseArgs | ParamNames -}}
  };
  return CompleterBase::SendReply(&_response.GetOutgoingMessage());
}
#endif
{{- end }}

{{- define "ReplyManagedResultSuccessMethodSignature" -}}
ReplySuccess({{ .Result.ValueMembers | Params }})
{{- end }}

{{- define "ReplyManagedResultSuccessMethodDefinition" }}
#ifdef __Fuchsia__
::fidl::Result
{{ .LLProps.ProtocolName.Name }}::Interface::{{ .Name }}CompleterBase::
    {{- template "ReplyManagedResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> _response;
  {{- range .Result.ValueMembers }}
  _response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply({{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&_response)));
}
#endif
{{- end }}

{{- define "ReplyManagedResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyManagedResultErrorMethodDefinition" }}
#ifdef __Fuchsia__
::fidl::Result
{{ .LLProps.ProtocolName.Name }}::Interface::{{ .Name }}CompleterBase::
    {{- template "ReplyManagedResultErrorMethodSignature" . }} {
  return Reply({{ .Result.ResultDecl }}::WithErr(::fidl::unowned_ptr(&error)));
}
#endif
{{- end }}
`
