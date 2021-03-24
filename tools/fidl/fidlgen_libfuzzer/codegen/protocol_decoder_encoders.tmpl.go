// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplProtocolDecoderEncoders = `
{{- define "ProtocolDecoderEncoders" -}}

{{- range .Methods -}}

{{- if .HasRequest }}
[](uint8_t* bytes, uint32_t num_bytes, zx_handle_info_t* handles, uint32_t num_handles) ->
  ::std::pair<zx_status_t, zx_status_t> {
  // Decode/re-encode protocol request.
  {{ $.Wire }}::{{ .Name }}Request::DecodedMessage decoded(bytes, num_bytes);  // protocol_decoder_encoder (1).
  if (decoded.status()) {
    {{ $.Wire }}::{{ .Name }}Request* value = decoded.PrimaryObject();
    {{ $.Wire }}::{{ .Name }}Request::OwnedEncodedMessage encoded(value);
    if (!encoded.status()) {
      return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
    }
    [[maybe_unused]] fidl_outgoing_msg_t* message = encoded.GetOutgoingMessage().message();
    // TODO: Verify re-encoded message matches initial message.
    return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
  }
  return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), ZX_ERR_INTERNAL);
},
{{- end -}}

{{- if .HasResponse }}
[](uint8_t* bytes, uint32_t num_bytes, zx_handle_info_t* handles, uint32_t num_handles) ->
  ::std::pair<zx_status_t, zx_status_t> {
  // Decode/re-encode protocol response.
  {{ $.Wire }}::{{ .Name }}Response::DecodedMessage decoded(bytes, num_bytes);
  if (decoded.status()) {
    {{ $.Wire }}::{{ .Name }}Response* value = decoded.PrimaryObject();
    {{ $.Wire }}::{{ .Name }}Response::OwnedEncodedMessage encoded(value);
    if (!encoded.status()) {
      return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
    }
    [[maybe_unused]] fidl_outgoing_msg_t* message = encoded.GetOutgoingMessage().message();
    // TODO: Verify re-encoded message matches initial message.
    return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
  }
  return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), ZX_ERR_INTERNAL);
},
{{- end -}}

{{- end -}}

{{- end -}}
`
