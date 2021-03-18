// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplProtocolDecoderEncoders = `
{{- define "ProtocolDecoderEncoders" -}}

{{- $ns := .Natural.Namespace -}}

{{- range .Methods -}}

{{- if .HasRequest }}
[](uint8_t* bytes, uint32_t num_bytes, zx_handle_info_t* handles, uint32_t num_handles) ->
  :std::pair<zx_status_t, zx_status_t> {
  {{ .Request.CodingTable.Wire }}::DecodedMessage decoded(bytes, num_bytes);
  if (decoded.status()) {
    {{ .Request.CodingTable.Wire }}* value = decoded.PrimaryObject();
    {{ .Request.CodingTable.Wire }}::OwnedByteEncodedMessage encoded(value);
    if (!encoded.status()) {
      return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
    }
    fidl_outgoing_message_t* message = encoded.GetOutgoingMessage.message();
    // TODO: Verify re-encoded message matches initial message.
    return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
  }
  return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), ZX_ERR_INTERNAL);
}
{{- if .HasResponse -}},{{- end -}}
{{- end -}}

{{- if .HasResponse }}
[](uint8_t* bytes, uint32_t num_bytes, zx_handle_info_t* handles, uint32_t num_handles) ->
  :std::pair<zx_status_t, zx_status_t> {
  {{ .Response.CodingTable.Wire }}::DecodedMessage decoded(bytes, num_bytes);
  if (decoded.status()) {
    {{ .Response.CodingTable.Wire }}* value = decoded.PrimaryObject();
    {{ .Request.CodingTable.Wire }}::{{ .Name }}::OwnedByteEncodedMessage encoded(value);
    if (!encoded.status()) {
      return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
    }
    fidl_outgoing_message_t* message = encoded.GetOutgoingMessage.message();
    // TODO: Verify re-encoded message matches initial message.
    return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
  }
  return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), ZX_ERR_INTERNAL);
},
{{- end -}}

{{- end -}}

{{- end -}}
`
