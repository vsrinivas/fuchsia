// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolEventHandlerTmpl contains the definitions for:
//  * fidl::internal::WireEventHandlerInterface<Protocol>
//  * fidl::WireAsyncEventHandler<Protocol>
//  * fidl::WireSyncEventHandler<Protocol>
const fragmentProtocolEventHandlerTmpl = `
{{- define "ProtocolEventHandlerDeclaration" }}
{{- EnsureNamespace "" }}
{{- IfdefFuchsia }}
template<>
class {{ .WireEventHandlerInterface }} {
public:
  {{ .WireEventHandlerInterface.Self }}() = default;
  virtual ~{{ .WireEventHandlerInterface.Self }}() = default;
  {{- range .Events -}}
    {{- .Docs }}
    virtual void {{ .Name }}({{ .WireResponse }}* event) {}
  {{- end }}
};

template<>
class {{ .WireAsyncEventHandler }}
    : public {{ .WireEventHandlerInterface }}, public ::fidl::internal::AsyncEventHandler {
 public:
 {{ .WireAsyncEventHandler.Self }}() = default;
};

template<>
class {{ .WireSyncEventHandler }} : public {{ .WireEventHandlerInterface }} {
public:
  {{ .WireSyncEventHandler.Self }}() = default;

  // Method called when an unknown event is found. This methods gives the status which, in this
  // case, is returned by HandleOneEvent.
  virtual zx_status_t Unknown() = 0;

  // Handle all possible events defined in this protocol.
  // Blocks to consume exactly one message from the channel, then call the corresponding virtual
  // method.
  ::fidl::Result HandleOneEvent(
      ::fidl::UnownedClientEnd<{{ . }}> client_end);
};
{{- EndifFuchsia }}
{{- end }}

`
