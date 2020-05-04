// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Helpers = `
{{- define "SetTransactionHeaderForResponse" }}
  {{ .LLProps.ProtocolName }}::SetTransactionHeaderFor::{{ .Name }}Response(
      ::fidl::DecodedMessage<{{ .Name }}Response>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              {{ .Name }}Response::PrimarySize,
              {{ .Name }}Response::PrimarySize)));
{{- end }}

{{- define "SetTransactionHeaderForRequestMethodSignature" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Request(const ::fidl::DecodedMessage<{{ $protocol_name}}::{{ .Name }}Request>& _msg)
{{- end }}

{{- define "SetTransactionHeaderForRequestMethodDefinition" -}}
{{- $protocol_name := .LLProps.ProtocolName -}}
void {{ $protocol_name }}::SetTransactionHeaderFor::{{ template "SetTransactionHeaderForRequestMethodSignature" . }} {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, {{ .Ordinals.Write.Name }});
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
{{- end }}

{{- define "SetTransactionHeaderForResponseMethodSignature" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Response(const ::fidl::DecodedMessage<{{ $protocol_name}}::{{ .Name }}Response>& _msg)
{{- end }}

{{- define "SetTransactionHeaderForResponseMethodDefinition" -}}
{{- $protocol_name := .LLProps.ProtocolName -}}
void {{ $protocol_name }}::SetTransactionHeaderFor::{{ template "SetTransactionHeaderForResponseMethodSignature" . }} {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, {{ .Ordinals.Write.Name }});
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
{{- end }}
`
