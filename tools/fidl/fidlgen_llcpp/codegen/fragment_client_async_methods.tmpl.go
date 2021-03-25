// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientAsyncMethodsTmpl = `
{{- define "AsyncClientAllocationComment" -}}
{{- $alloc := .Request.ClientAllocation }}
{{- if $alloc.IsStack -}}
Allocates {{ $alloc.Size }} bytes of request buffer on the stack. The callback is stored on the heap.
{{- else -}}
The request and callback are allocated on the heap.
{{- end }}
{{- end }}

{{- define "ClientAsyncRequestManagedCallbackSignature" -}}
::fit::callback<void ({{ .WireResponse }}* response)>
{{- end }}

{{- define "ClientAsyncRequestManagedMethodArguments" -}}
{{ .RequestArgs | Params }}{{ if .RequestArgs }}, {{ end }} {{- template "ClientAsyncRequestManagedCallbackSignature" . }} _cb
{{- end }}

{{- define "ClientAsyncRequestCallerAllocateMethodArguments" -}}
{{ template "CallerBufferParams" .RequestArgs }}{{ if .RequestArgs }}, {{ end }}{{ .WireResponseContext }}* _context
{{- end }}

{{- define "ClientAsyncRequestManagedMethodDefinition" }}
#ifdef __Fuchsia__
{{ .WireResponseContext }}::{{ .WireResponseContext.Self }}()
    : ::fidl::internal::ResponseContext({{ .WireResponse }}::Type, {{ .OrdinalName }}) {}

void {{ .WireResponseContext }}::OnReply(uint8_t* reply) {
  OnReply(reinterpret_cast<{{ .WireResponse }}*>(reply));
}

::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}(
    {{ template "ClientAsyncRequestManagedMethodArguments" . }}) {
  class ResponseContext final : public {{ .WireResponseContext }} {
   public:
    ResponseContext({{ template "ClientAsyncRequestManagedCallbackSignature" . }} cb)
        : cb_(std::move(cb)) {}

    void OnReply({{ .WireResponse }}* response) override {
      cb_(response);
      {{ if and .HasResponse .Response.IsResource }}
      response->_CloseHandles();
      {{ end }}
      delete this;
    }

    void OnError() override {
      delete this;
    }

   private:
    {{ template "ClientAsyncRequestManagedCallbackSignature" . }} cb_;
  };

  auto* _context = new ResponseContext(std::move(_cb));
  ::fidl::internal::ClientBase::PrepareAsyncTxn(_context);
  {{ .WireRequest }}::OwnedEncodedMessage _request(_context->Txid()
  {{- .RequestArgs | CommaParamNames -}}
  );
  return _request.GetOutgoingMessage().Write(this, _context);
}

::fidl::Result {{ .Protocol.WireClientImpl.NoLeading }}::{{ .Name }}({{ template "ClientAsyncRequestCallerAllocateMethodArguments" . }}) {
  ::fidl::internal::ClientBase::PrepareAsyncTxn(_context);
  {{ if .RequestArgs }}
  {{ .WireRequest }}::UnownedEncodedMessage _request(_request_buffer.data, _request_buffer.capacity, _context->Txid()
  {{- else }}
  {{ .WireRequest }}::OwnedEncodedMessage _request(_context->Txid()
  {{- end }}
  {{- .RequestArgs | CommaParamNames -}}
  );
  return _request.GetOutgoingMessage().Write(this, _context);
}
#endif
{{- end }}
`
