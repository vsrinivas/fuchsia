// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ReplyInPlace = `
{{- define "ReplyInPlaceMethodSignature" -}}
Reply(::fidl::DecodedMessage<{{ .Name }}Response> params)
{{- end }}

{{- define "ReplyInPlaceMethodDefinition" }}
void {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyInPlaceMethodSignature" . }} {
  {{ .LLProps.ProtocolName }}::SetTransactionHeaderFor::{{ .Name }}Response(params);
  CompleterBase::SendReply(std::move(params));
}
{{- end }}
`
