// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ReplyInPlace = `
{{- define "ReplyInPlaceMethodSignature" -}}
Reply(::fidl::DecodedMessage<{{ .Name }}Response> params)
{{- end }}

{{- define "ReplyInPlaceMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyInPlaceMethodSignature" . }} {
  ZX_DEBUG_ASSERT(params.message()->_hdr.magic_number == kFidlWireFormatMagicNumberInitial);
  ZX_DEBUG_ASSERT(params.message()->_hdr.ordinal == {{ .OrdinalName }});
  return CompleterBase::SendReply(std::move(params));
}
{{- end }}
`
