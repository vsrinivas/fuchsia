// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolCallerTmpl contains the definition for
// fidl::internal::WireCaller<Protocol>.
const fragmentProtocolCallerTmpl = `
{{- define "Protocol:Caller:MessagingHeader" }}
{{- EnsureNamespace "" }}

// Methods to make a sync FIDL call directly on an unowned channel or a
// const reference to a |fidl::ClientEnd<{{ .WireType }}>|,
// avoiding setting up a client.
template<>
class {{ .WireCaller }} final {
 public:
  explicit {{ .WireCaller.Self }}(::fidl::UnownedClientEnd<{{ . }}> client_end) :
    client_end_(client_end) {}
{{ "" }}
  {{- /* Client-calling functions do not apply to events. */}}
  {{- range .ClientMethods }}
    {{ .Docs }}
    //{{ template "Method:ClientAllocationComment:Helper" . }}
    static {{ .WireResult }} {{ .Name }}(
        {{- RenderParams (printf "::fidl::UnownedClientEnd<%s> _client_end" .Protocol)
                         .RequestArgs }}) {
      return {{ .WireResult }}({{ RenderForwardParams "_client_end" .RequestArgs }});
    }

    {{ .Docs }}
    //{{ template "Method:ClientAllocationComment:Helper" . }}
    {{ .WireResult }} {{ .Name }}({{- RenderParams .RequestArgs }}) && {
      return {{ .WireResult }}({{ RenderForwardParams "client_end_" .RequestArgs }});
    }
{{ "" }}
    {{- if or .RequestArgs .ResponseArgs }}

      {{- $call_args := (List) }}
      {{- if .RequestArgs }}
        {{- $call_args = (List $call_args "::fidl::BufferSpan _request_buffer" .RequestArgs) }}
      {{- end }}
      {{- if .HasResponse }}
        {{- $call_args = (List $call_args "::fidl::BufferSpan _response_buffer") }}
      {{- end }}

      {{- $result_args := (List) }}
      {{- if .RequestArgs }}
        {{- $result_args = (List $result_args "_request_buffer.data" "_request_buffer.capacity") }}
      {{- end }}
      {{- $result_args = (List $result_args .RequestArgs) }}
      {{- if .HasResponse -}}
        {{- $result_args = (List $result_args "_response_buffer.data" "_response_buffer.capacity") }}
      {{- end }}

      {{ .Docs }}
      // Caller provides the backing storage for FIDL message via request and response buffers.
      static {{ .WireUnownedResult }} {{ .Name }}(
          {{- RenderParams (printf "::fidl::UnownedClientEnd<%s> _client_end" .Protocol)
                           $call_args }}) {
        return {{ .WireUnownedResult }}({{ RenderForwardParams "_client_end" $result_args }});
      }

      {{- .Docs }}
      // Caller provides the backing storage for FIDL message via request and response buffers.
      {{ .WireUnownedResult }} {{ .Name }}({{ RenderParams $call_args }}) && {
        return {{ .WireUnownedResult }}({{ RenderForwardParams "client_end_" $result_args }});
      }

    {{- end }}
{{ "" }}
  {{- end }}
 private:
  ::fidl::UnownedClientEnd<{{ . }}> client_end_;
};
{{- end }}
`
