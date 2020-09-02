// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSendEventInPlaceTmpl = `
{{- define "SendEventInPlaceMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _chan, ::fidl::DecodedMessage<{{ .Name }}Response> params)
{{- end }}

{{- define "SendEventInPlaceMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::{{ template "SendEventInPlaceMethodSignature" . }} {
  ZX_ASSERT(params.message()->_hdr.magic_number == kFidlWireFormatMagicNumberInitial);
  ZX_ASSERT(params.message()->_hdr.ordinal == {{ .OrdinalName }});
  return ::fidl::Write(::zx::unowned_channel(_chan), std::move(params));
}
{{- end }}
`
