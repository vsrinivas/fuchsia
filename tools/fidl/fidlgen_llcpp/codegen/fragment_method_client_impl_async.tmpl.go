// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentMethodClientImplAsyncTmpl = `
{{- define "Method:ClientImplAsync:Header" }}
  {{- IfdefFuchsia -}}
  {{- /* Async managed flavor */}}
  {{- .Docs }}
  {{- if .DocComments }}
    //
  {{- end }}
  // Asynchronous variant of |{{ .Protocol.Name }}.{{ .Name }}()|.
  {{- if .Request.ClientAllocationV1.IsStack }}
    // Allocates {{ .Request.ClientAllocationV1.Size }} bytes of request buffer on the stack. The callback is stored on the heap.
  {{- else }}
    // The request and callback are allocated on the heap.
  {{- end }}
  void {{ .Name }}({{ RenderParams .RequestArgs
                    (printf "::fidl::WireClientCallback<%s> _cb" .Marker) }});

  void {{ .Name }}({{ RenderParams .RequestArgs
    (printf "::fit::callback<void (%s* response)> _cb" .WireResponse) }});

  {{- /* TODO(fxbug.dev/75324): Remove after migrating non-fuchsia.git uses */}}
  void {{ .Name }}({{ RenderParams .RequestArgs
      (printf "::fit::callback<void (%s&& result)> _cb" .WireUnownedResult) }});

  {{- /* Async caller-allocate flavor */}}
  {{ .Docs }}
  {{- if .DocComments }}
    //
  {{- end }}
  // Asynchronous variant of |{{ .Protocol.Name }}.{{ .Name }}()|.
  // Caller provides the backing storage for FIDL message via request buffer.
  // Ownership of |_context| is given unsafely to the binding until |OnError|
  // or |OnReply| are called on it.
  void {{ .Name }}(
        {{- if .RequestArgs }}
          {{ RenderParams "::fidl::BufferSpan _request_buffer"
                          .RequestArgs
                          (printf "%s* _context" .WireResponseContext) }}
        {{- else }}
          {{ .WireResponseContext }}* _context
        {{- end -}}
    );
  {{- EndifFuchsia -}}
{{- end }}


{{- define "Method:ClientImplAsync:Source" }}
  {{- IfdefFuchsia -}}

  {{- /* Async managed flavor */}}
  void {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
    {{ RenderParams .RequestArgs
                    (printf "::fidl::WireClientCallback<%s> _cb" .Marker) }}) {
    using Callback = decltype(_cb);
    class ResponseContext final : public {{ .WireResponseContext }} {
    public:
      ResponseContext(Callback cb)
          : cb_(std::move(cb)) {}

      void OnResult({{ .WireUnownedResult }}& result) override {
        cb_(result);
        delete this;
      }

    private:
      Callback cb_;
    };

    auto* _context = new ResponseContext(std::move(_cb));
    FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
    {{ .WireRequest }}::OwnedEncodedMessage _request(
      {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" .RequestArgs -}}
    );
    ::fidl::internal::ClientBase::SendTwoWay(_request.GetOutgoingMessage(), _context);
  }

  {{- /* TODO(fxbug.dev/75324): Remove after migrating non-fuchsia.git uses */}}
  void {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
    {{ RenderParams .RequestArgs
      (printf "::fit::callback<void (%s&& result)> _cb" .WireUnownedResult) }}) {
    auto _forwarder = [_cb = std::move(_cb)] ({{ .WireUnownedResult }}& _result) mutable {
      _cb(std::move(_result));
    };
    {{ .Name }}({{ RenderForwardParams .RequestArgs "std::move(_forwarder)" }});
  }

  void {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
    {{ RenderParams .RequestArgs
      (printf "::fit::callback<void (%s* response)> _cb" .WireResponse) }}) {
    using Callback = decltype(_cb);
    class ResponseContext final : public {{ .WireResponseContext }} {
    public:
      ResponseContext(Callback cb)
          : cb_(std::move(cb)) {}

      void OnResult({{ .WireUnownedResult }}& result) override {
        if (result.ok()) {
          ::fidl::WireResponse<{{ .Marker }}>* response = result.Unwrap();
          cb_(response);
        }
        delete this;
      }

    private:
      Callback cb_;
    };

    auto* _context = new ResponseContext(std::move(_cb));
    FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
    {{ .WireRequest }}::OwnedEncodedMessage _request(
      {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" .RequestArgs -}}
    );
    ::fidl::internal::ClientBase::SendTwoWay(_request.GetOutgoingMessage(), _context);
  }


  {{- /* Async caller-allocate flavor */}}
  void {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
      {{- if .RequestArgs }}
        {{ RenderParams "::fidl::BufferSpan _request_buffer"
                        .RequestArgs
                        (printf "%s* _context" .WireResponseContext) }}
      {{- else }}
        {{ .WireResponseContext }}* _context
      {{- end -}}) {
    {{ if .RequestArgs }}
      {{ .WireRequest }}::UnownedEncodedMessage _request(
        {{ RenderForwardParams "_request_buffer.data"
                              "_request_buffer.capacity"
                              .RequestArgs }});
    {{- else }}
      FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
      {{ .WireRequest }}::OwnedEncodedMessage _request(
        {{ RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" .RequestArgs }});
    {{- end }}
    ::fidl::internal::ClientBase::SendTwoWay(_request.GetOutgoingMessage(), _context);
  }

  {{- EndifFuchsia -}}
{{- end }}

`
