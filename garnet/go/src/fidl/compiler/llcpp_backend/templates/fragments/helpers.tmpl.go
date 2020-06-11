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

{{- define "SetTransactionHeaderForRequestMethodDeclarationSignatureDecodedMessage" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Request(const ::fidl::DecodedMessage<{{ $protocol_name}}::{{ .Name }}Request>& _msg, zx_txid_t _txid = 0)
{{- end }}

{{- define "SetTransactionHeaderForRequestMethodDeclarationSignatureEncodedMessage" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Request(const ::fidl::EncodedMessage<{{ $protocol_name}}::{{ .Name }}Request>& _msg, zx_txid_t _txid = 0)
{{- end }}

{{- define "SetTransactionHeaderForRequestMethodDefinitionSignatureDecodedMessage" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Request(const ::fidl::DecodedMessage<{{ $protocol_name}}::{{ .Name }}Request>& _msg, zx_txid_t _txid)
{{- end }}

{{- define "SetTransactionHeaderForRequestMethodDefinitionSignatureEncodedMessage" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Request(const ::fidl::EncodedMessage<{{ $protocol_name}}::{{ .Name }}Request>& _msg, zx_txid_t _txid)
{{- end }}

{{- define "SetTransactionHeaderForRequestMethodDefinition" -}}
{{- $protocol_name := .LLProps.ProtocolName -}}
void {{ $protocol_name }}::SetTransactionHeaderFor::{{ template "SetTransactionHeaderForRequestMethodDefinitionSignatureDecodedMessage" . }} {
  fidl_init_txn_header(&_msg.message()->_hdr, _txid, {{ .Ordinals.Write.Name }});
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
void {{ $protocol_name }}::SetTransactionHeaderFor::{{ template "SetTransactionHeaderForRequestMethodDefinitionSignatureEncodedMessage" . }} {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(_msg.bytes().data());
  fidl_init_txn_header(hdr, _txid, {{ .Ordinals.Write.Name }});
  hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
{{- end }}

{{- define "SetTransactionHeaderForResponseMethodSignatureDecodedMessage" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Response(const ::fidl::DecodedMessage<{{ $protocol_name}}::{{ .Name }}Response>& _msg)
{{- end }}
{{- define "SetTransactionHeaderForResponseMethodSignatureEncodedMessage" }}
{{- $protocol_name := .LLProps.ProtocolName -}}
{{- .Name }}Response(const ::fidl::EncodedMessage<{{ $protocol_name}}::{{ .Name }}Response>& _msg)
{{- end }}

{{- define "SetTransactionHeaderForResponseMethodDefinition" -}}
{{- $protocol_name := .LLProps.ProtocolName -}}
void {{ $protocol_name }}::SetTransactionHeaderFor::{{ template "SetTransactionHeaderForResponseMethodSignatureDecodedMessage" . }} {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, {{ .Ordinals.Write.Name }});
  _msg.message()->_hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
void {{ $protocol_name }}::SetTransactionHeaderFor::{{ template "SetTransactionHeaderForResponseMethodSignatureEncodedMessage" . }} {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(_msg.bytes().data());
  fidl_init_txn_header(hdr, 0, {{ .Ordinals.Write.Name }});
  hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
}
{{- end }}
`
