// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyManagedTmpl = `
{{- define "ReplyManagedMethodSignature" -}}
Reply({{ .ResponseArgs | Params }})
{{- end }}

{{- define "ReplyManagedMethodDefinition" }}
{{ EnsureNamespace "" }}
#ifdef __Fuchsia__
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::
    {{- template "ReplyManagedMethodSignature" . }} {
  ::fidl::OwnedEncodedMessage<{{ .WireResponse }}> _response{
    {{- .ResponseArgs | ParamNames -}}
  };
  return {{ .WireCompleterBase }}::SendReply(&_response.GetOutgoingMessage());
}
#endif
{{- end }}

{{- define "ReplyManagedResultSuccessMethodSignature" -}}
ReplySuccess({{ .Result.ValueMembers | Params }})
{{- end }}

{{- define "ReplyManagedResultSuccessMethodDefinition" }}
{{ EnsureNamespace "" }}
#ifdef __Fuchsia__
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::
    {{- template "ReplyManagedResultSuccessMethodSignature" . }} {
  {{ .Result.ValueStructDecl }} _response;
  {{- range .Result.ValueMembers }}
  _response.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply({{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&_response)));
}
#endif
{{- end }}

{{- define "ReplyManagedResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyManagedResultErrorMethodDefinition" }}
{{ EnsureNamespace "" }}
#ifdef __Fuchsia__
::fidl::Result
{{ .WireCompleterBase.NoLeading }}::
    {{- template "ReplyManagedResultErrorMethodSignature" . }} {
  return Reply({{ .Result.ResultDecl }}::WithErr(::fidl::unowned_ptr(&error)));
}
#endif
{{- end }}
`
