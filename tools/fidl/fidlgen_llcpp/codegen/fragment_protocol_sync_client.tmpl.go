// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolInterfaceTmpl contains the definition for
// fidl::WireSyncClient<Protocol>.
const fragmentProtocolSyncClientTmpl = `
{{- define "Protocol:SyncClient:MessagingHeader" }}
{{- EnsureNamespace "" }}
template<>
class {{ .WireSyncClient }} final {
  public:
   WireSyncClient() = default;

   explicit WireSyncClient(::fidl::ClientEnd<{{ . }}> client_end)
       : client_end_(std::move(client_end)) {}

   ~WireSyncClient() = default;
   WireSyncClient(WireSyncClient&&) = default;
   WireSyncClient& operator=(WireSyncClient&&) = default;

   const ::fidl::ClientEnd<{{ . }}>& client_end() const { return client_end_; }
   ::fidl::ClientEnd<{{ . }}>& client_end() { return client_end_; }

   const ::zx::channel& channel() const { return client_end_.channel(); }
   ::zx::channel* mutable_channel() { return &client_end_.channel(); }

   {{- /* Client-calling functions do not apply to events. */}}
   {{- range .ClientMethods }}
   {{ .Docs }}
   //{{ template "Method:ClientAllocationComment:Helper" . }}
   {{ .WireResult }} {{ .Name }}({{ RenderParams .RequestArgs }}) {
     return {{ .WireResult }}({{ RenderForwardParams "this->client_end()" .RequestArgs }});
   }

   {{- if or .RequestArgs .ResponseArgs }}
    {{ .Docs }}
    // Caller provides the backing storage for FIDL message via request and response buffers.
    {{ .WireUnownedResult }} {{ .Name }}({{ template "Method:ClientImplSyncCallerAllocateArguments:Helper" . }}) {
    {{- $args := (List "this->client_end()") }}
    {{- if .RequestArgs }}
      {{- $args = (List $args "_request_buffer.data" "_request_buffer.capacity") }}
    {{- end -}}
    {{- $args = (List $args .RequestArgs) }}
    {{- if .HasResponse }}
      {{- $args = (List $args "_response_buffer.data" "_response_buffer.capacity") }}
    {{- end }}
    return {{ .WireUnownedResult }}({{ RenderForwardParams $args }});
   }
     {{- end }}

  {{- end }}
  {{- if .Events }}
   // Handle all possible events defined in this protocol.
   // Blocks to consume exactly one message from the channel, then call the corresponding virtual
   // method defined in |SyncEventHandler|. The return status of the handler function is folded with
   // any transport-level errors and returned.
   ::fidl::Result HandleOneEvent({{ .WireSyncEventHandler  }}& event_handler) {
     return event_handler.HandleOneEvent(client_end_);
   }
   {{- end }}
  private:
    ::fidl::ClientEnd<{{ . }}> client_end_;
};
{{- end }}
`
