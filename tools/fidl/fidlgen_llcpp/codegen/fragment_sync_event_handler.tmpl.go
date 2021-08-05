// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncEventHandlerTmpl = `
{{- define "EventHandlerHandleOneEventMethodDefinition" }}
{{ EnsureNamespace "" }}
::fidl::Result {{ .WireSyncEventHandler.NoLeading }}::HandleOneEvent(
    ::fidl::UnownedClientEnd<{{ . }}> client_end) {
  zx_status_t status = client_end.channel()->wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                                      ::zx::time::infinite(),
                                                      nullptr);
  if (status != ZX_OK) {
    return ::fidl::Result::TransportError(status, ::fidl::internal::kErrorWaitOneFailed);
  }
  constexpr uint32_t kHandleAllocSize = ([]() constexpr {
    uint32_t x = 0;
    {{- range .Events }}
    if ({{ .WireResponse }}::MaxNumHandles >= x) {
      x = {{ .WireResponse }}::MaxNumHandles;
    }
    {{- end }}
    if (x > ZX_CHANNEL_MAX_MSG_HANDLES) {
      x = ZX_CHANNEL_MAX_MSG_HANDLES;
    }
    return x;
  })();
  static_assert(kHandleAllocSize <= ZX_CHANNEL_MAX_MSG_HANDLES);
  {{ .SyncEventAllocationV1.BackingBufferType }} read_storage;
  std::array<zx_handle_info_t, kHandleAllocSize> read_handles;
  ::fidl::IncomingMessage msg = fidl::ChannelReadEtc(
      client_end.handle(),
      ZX_CHANNEL_READ_MAY_DISCARD,
      read_storage.view(),
      cpp20::span(read_handles)
  );
  if (msg.status() == ZX_ERR_BUFFER_TOO_SMALL) {
    // Message size is unexpectedly larger than calculated.
    // This can only be due to a newer version of the protocol defining a new event,
    // whose size exceeds the maximum of known events in the current protocol.
    return ::fidl::Result::UnexpectedMessage(Unknown());
  }
  if (!msg.ok()) {
    return msg;
  }
  fidl_message_header_t* hdr = msg.header();
  switch (hdr->ordinal) {
  {{- range .Events }}
    case {{ .OrdinalName }}: {
      ::fidl::DecodedMessage<{{ .WireResponse }}> decoded{::std::move(msg)};
      if (!decoded.ok()) {
        return ::fidl::Result(decoded);
      }
      {{ .Name }}(decoded.PrimaryObject());
      return ::fidl::Result::Ok();
    }
  {{- end }}
    default: {
      return ::fidl::Result::UnexpectedMessage(Unknown());
    }
  }
}
{{- end }}
`
