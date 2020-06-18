// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Helpers = `
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
  fidl_init_txn_header(&_msg.message()->_hdr, _txid, {{ .OrdinalName }});
}
void {{ $protocol_name }}::SetTransactionHeaderFor::{{ template "SetTransactionHeaderForRequestMethodDefinitionSignatureEncodedMessage" . }} {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(_msg.bytes().data());
  fidl_init_txn_header(hdr, _txid, {{ .OrdinalName }});
}
{{- end }}
`
