// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplDecoderEncoder = `
{{- define "DecoderEncoder" -}}
{{- $ns := .Namespace -}}
[](uint8_t* bytes, uint32_t num_bytes, zx_handle_info_t* handles, uint32_t num_handles) ->
  :std::pair<zx_status_t, zx_status_t> {
  ::llcpp::{{ $ns }}::{{ .Name }}::DecodedMessage decoded(bytes, num_bytes);

  if (decoded.status() != ZX_OK) {
    return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), ZX_ERR_INTERNAL);
  }

  ::llcpp::{{ $ns }}::{{ .Name }}* value = decoded.PrimaryObject();
  ::llcpp::{{ $ns }}::{{ .Name }}::OwnedEncodedMessage encoded(value);

  if (encoded.status() != ZX_OK) {
    return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
  }

  fidl_outgoing_message_t* message = encoded.GetOutgoingMessage.message();
  // TODO: Verify re-encoded message matches initial message.
  return ::std::make_pair<zx_status_t, zx_status_t>(decoded.status(), encoded.status());
}
{{- end -}}
`
