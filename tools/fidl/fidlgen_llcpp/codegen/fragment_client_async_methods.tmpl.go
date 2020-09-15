// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientAsyncMethodsTmpl = `
{{- define "AsyncClientAllocationComment" -}}
{{- $context := .LLProps.ClientContext }}
{{- if $context.StackAllocRequest -}} Allocates {{ $context.StackUseRequest }} bytes of request buffer on the stack. The callback is stored on the heap.
{{- else -}} The request and callback are allocated on the heap.
{{- end }}
{{- end }}

{{- define "AsyncEventHandlerIndividualMethodSignature" -}}
  {{- if .Response -}}
({{ template "Params" .Response }})
  {{- else -}}
()
  {{- end -}}
{{- end }}

{{- define "AsyncEventHandlerMoveParams" }}
  {{- range $index, $param := . }}
    {{- if $index }}, {{ end -}} std::move(message->{{ $param.Name }})
  {{- end }}
{{- end }}

{{- define "ClientAsyncRequestManagedCallbackSignature" -}}
::fit::callback<void {{- template "AsyncEventHandlerIndividualMethodSignature" . }}>
{{- end }}

{{- define "ClientAsyncRequestManagedMethodArguments" -}}
{{ template "Params" .Request }}{{ if .Request }}, {{ end }} {{- template "ClientAsyncRequestManagedCallbackSignature" . }} _cb
{{- end }}

{{- define "ClientAsyncRequestCallerAllocateMethodArguments" -}}
{{ template "CallerBufferParams" .Request }}{{ if .Request }}, {{ end }}{{ .Name }}ResponseContext* _context
{{- end }}

{{- define "ClientAsyncRequestManagedMethodDefinition" }}
{{ .LLProps.ProtocolName }}::{{ .Name }}ResponseContext::{{ .Name }}ResponseContext()
    : ::fidl::internal::ResponseContext({{ .Name }}Response::Type, {{ .OrdinalName }}) {}

void {{ .LLProps.ProtocolName }}::{{ .Name }}ResponseContext::OnReply(uint8_t* reply) {
  OnReply(reinterpret_cast<{{ .Name }}Response*>(reply));
}

::fidl::Result {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}({{ template "ClientAsyncRequestManagedMethodArguments" . }}) {
  class ResponseContext final : public {{ .Name }}ResponseContext {
   public:
    ResponseContext({{ template "ClientAsyncRequestManagedCallbackSignature" . }} cb) : cb_(std::move(cb)) {}

    void OnReply({{ .Name }}Response* message) override {
      cb_({{ template "AsyncEventHandlerMoveParams" .Response }});
      {{ if and .HasResponse .ResponseIsResource }}
      message->_CloseHandles();
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
  {{ .Name }}OwnedRequest _request(_context->Txid()
  {{- template "CommaPassthroughMessageParams" .Request -}}
  );
  return _request.GetFidlMessage().Write(this, _context);
}

::fidl::Result {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}({{ template "ClientAsyncRequestCallerAllocateMethodArguments" . }}) {
  ::fidl::internal::ClientBase::PrepareAsyncTxn(_context);
  {{ if .Request }}
  {{ .Name }}UnownedRequest _request(_request_buffer.data(), _request_buffer.capacity(), _context->Txid()
  {{- else }}
  {{ .Name }}OwnedRequest _request(_context->Txid()
  {{- end }}
  {{- template "CommaPassthroughMessageParams" .Request -}}
  );
  return _request.GetFidlMessage().Write(this, _context);
}
{{- end }}
`
