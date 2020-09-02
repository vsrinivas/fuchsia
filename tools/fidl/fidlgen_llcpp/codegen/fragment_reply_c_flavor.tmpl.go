// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentReplyCFlavorTmpl = `
{{- define "ReplyCFlavorMethodSignature" -}}
Reply({{ template "Params" .Response }})
{{- end }}

{{- define "ReplyCFlavorMethodDefinition" }}
::fidl::Result {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorMethodSignature" . }} {
  {{ .Name }}OwnedResponse _response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  return CompleterBase::SendReply(_response.GetFidlMessage());
}
{{- end }}

{{- define "ReplyCFlavorResultSuccessMethodSignature" -}}
ReplySuccess({{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyCFlavorResultSuccessMethodDefinition" }}
::fidl::Result {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> response;
  {{- range .Result.ValueMembers }}
  response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return Reply({{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&response)));
}
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodDefinition" }}
::fidl::Result {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultErrorMethodSignature" . }} {
  return Reply({{ .Result.ResultDecl }}::WithErr(::fidl::unowned_ptr(&error)));
}
{{- end }}
`
