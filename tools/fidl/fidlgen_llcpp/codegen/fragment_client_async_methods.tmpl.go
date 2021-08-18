// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientAsyncMethodsTmpl = `
{{- define "AsyncClientAllocationComment" -}}
{{- $alloc := .Request.ClientAllocationV1 }}
{{- if $alloc.IsStack -}}
Allocates {{ $alloc.Size }} bytes of request buffer on the stack. The callback is stored on the heap.
{{- else -}}
The request and callback are allocated on the heap.
{{- end }}
{{- end }}

{{- define "ClientAsyncRequestManagedMethodDefinition" }}
{{- IfdefFuchsia -}}

::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
  {{ RenderParams .RequestArgs
                  (printf "::fidl::WireClientCallback<%s> _cb" .Marker) }}) {
  using Callback = decltype(_cb);
  class ResponseContext final : public {{ .WireResponseContext }} {
   public:
    ResponseContext(Callback cb)
        : cb_(std::move(cb)) {}

    void OnResult({{ .WireUnownedResult }}&& result) override {
      cb_(std::move(result));
      delete this;
    }

   private:
    Callback cb_;
  };

  auto* _context = new ResponseContext(std::move(_cb));
  ::fidl::internal::ClientBase::PrepareAsyncTxn(_context);
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
  {{ .WireRequest }}::OwnedEncodedMessage _request(
    {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" "_context->Txid()" .RequestArgs -}}
  );
  return _request.GetOutgoingMessage().Write(this, _context);
}

::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
  {{ RenderParams .RequestArgs
    (printf "::fit::callback<void (%s* response)> _cb" .WireResponse) }}) {
  using Callback = decltype(_cb);
  class ResponseContext final : public {{ .WireResponseContext }} {
   public:
    ResponseContext(Callback cb)
        : cb_(std::move(cb)) {}

    void OnResult({{ .WireUnownedResult }}&& result) override {
      if (result.ok()) {
        ::fidl::WireResponse<{{ .Marker }}>* response = result.Unwrap();
        cb_(std::move(response));
      }
      delete this;
    }

   private:
    Callback cb_;
  };

  auto* _context = new ResponseContext(std::move(_cb));
  ::fidl::internal::ClientBase::PrepareAsyncTxn(_context);
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
  {{ .WireRequest }}::OwnedEncodedMessage _request(
    {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" "_context->Txid()" .RequestArgs -}}
  );
  return _request.GetOutgoingMessage().Write(this, _context);

}

::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
        {{- if .RequestArgs }}
          {{ RenderParams "::fidl::BufferSpan _request_buffer"
                          .RequestArgs
                          (printf "%s* _context" .WireResponseContext) }}
        {{- else }}
          {{ .WireResponseContext }}* _context
        {{- end -}}
    ) {
  ::fidl::internal::ClientBase::PrepareAsyncTxn(_context);
  {{ if .RequestArgs }}
    {{ .WireRequest }}::UnownedEncodedMessage _request(
      {{ RenderForwardParams "_request_buffer.data"
                             "_request_buffer.capacity"
                             "_context->Txid()"
                             .RequestArgs }});
  {{- else }}
    FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
    {{ .WireRequest }}::OwnedEncodedMessage _request(
      {{ RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" "_context->Txid()" .RequestArgs }});
  {{- end }}
  return _request.GetOutgoingMessage().Write(this, _context);
}
{{- EndifFuchsia -}}
{{- end }}
`
