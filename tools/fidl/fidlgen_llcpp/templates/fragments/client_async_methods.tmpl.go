// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ClientAsyncMethods = `
{{- define "AsyncClientAllocationComment" -}}
{{- $context := .LLProps.ClientContext }}
{{- if $context.StackAllocRequest -}} Allocates {{ $context.StackUseRequest }} bytes of request buffer on the stack. The callback is stored on the heap.
{{- else -}} The request and callback are allocated on the heap.
{{- end }}
{{- end }}

{{- define "ClientAsyncRequestManagedCallbackSignature" -}}
::fit::callback<void {{- template "SyncEventHandlerIndividualMethodSignature" . }}>
{{- end }}

{{- define "ClientAsyncRequestManagedMethodArguments" -}}
{{ template "Params" .Request }}{{ if .Request }}, {{ end }} {{- template "ClientAsyncRequestManagedCallbackSignature" . }} _cb
{{- end }}

{{- define "ClientAsyncRequestCallerAllocateMethodArguments" -}}
{{ template "CallerBufferParams" .Request }}{{ if .Request }}, {{ end }}{{ .Name }}ResponseContext* _context
{{- end }}

{{- define "AsyncEventHandlerInPlaceMethodSignature" -}}
  {{- if .Response -}}
(::fidl::DecodedMessage<{{ .Name }}Response> msg)
  {{- else -}}
()
  {{- end -}}
{{- end }}

{{- define "ClientAsyncRequestCallerAllocateMethodDefinition" }}
::fidl::StatusAndError {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}({{ template "ClientAsyncRequestCallerAllocateMethodArguments" . }}) {
  {{- if not .Request }}
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof({{ .Name }}Request)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  {{- else }}
  if (_request_buffer.capacity() < {{ .Name }}Request::PrimarySize) {
    return ::fidl::StatusAndError(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
  {{- end }}

  {{- if .LLProps.LinearizeRequest }}
  {{ .Name }}Request _request{
  {{- template "PassthroughParams" .Request -}}
  };
  {{- else }}
  new (_request_buffer.data()) {{ .Name }}Request{
  {{- template "PassthroughParams" .Request -}}
  };
  {{- end }}

  ::fidl::internal::ClientBase::PrepareAsyncTxn(_context);
  {{- if .LLProps.LinearizeRequest }}
  auto _encode_request_result = ::fidl::LinearizeAndEncode<{{ .Name }}Request>(&_request, std::move(_request_buffer));
  if (_encode_request_result.status != ZX_OK) {
    ::fidl::internal::ClientBase::ForgetAsyncTxn(_context);
    return std::move(_encode_request_result);
  }
  SetTransactionHeaderFor::{{ .Name }}Request(_encode_request_result.message, _context->Txid());
  {{- else }}
  _request_buffer.set_actual(sizeof({{ .Name }}Request));
  ::fidl::DecodedMessage<{{ .Name }}Request> _decoded_request(std::move(_request_buffer));
  SetTransactionHeaderFor::{{ .Name }}Request(_decoded_request, _context->Txid());
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    ::fidl::internal::ClientBase::ForgetAsyncTxn(_context);
    return ::fidl::DecodeResult<{{ .Name }}Response>::FromFailure(std::move(_encode_request_result));
  }
  {{- end }}

  if (auto _binding = ::fidl::internal::ClientBase::GetBinding()) {
    zx_status_t _write_status =
        ::fidl::Write(_binding->channel(), std::move(_encode_request_result.message));
    if (_write_status != ZX_OK) {
      ::fidl::internal::ClientBase::ForgetAsyncTxn(_context);
      return ::fidl::StatusAndError(_write_status, ::fidl::kErrorWriteFailed);
    }
    return ::fidl::StatusAndError(ZX_OK, nullptr);
  }
  ::fidl::internal::ClientBase::ForgetAsyncTxn(_context);
  return ::fidl::StatusAndError(ZX_ERR_CANCELED, ::fidl::kErrorChannelUnbound);
}
{{- end }}

{{- define "ClientAsyncRequestManagedMethodDefinition" }}
::fidl::StatusAndError {{ .LLProps.ProtocolName }}::ClientImpl::{{ .Name }}({{ template "ClientAsyncRequestManagedMethodArguments" . }}) {
  class ManagedResponseContext : public {{ .Name }}ResponseContext {
   public:
    ManagedResponseContext({{ template "ClientAsyncRequestManagedCallbackSignature" . }} cb) : cb_(std::move(cb)) {}

    void OnReply {{- template "AsyncEventHandlerInPlaceMethodSignature" . }} override {
      {{- if .Response }}
      auto message = msg.message();
      {{- end }}
      cb_({{ template "SyncEventHandlerMoveParams" .Response }});
      delete this;
    }

    void OnError() override { delete this; }

    {{ template "ClientAsyncRequestManagedCallbackSignature" . }} cb_;
  };

  {{- if .Request }}
  constexpr uint32_t _kWriteAllocSize =
      ::fidl::internal::ClampedMessageSize<{{ .Name }}Request,
                                           ::fidl::MessageDirection::kSending>();
    {{- if .LLProps.ClientContext.StackAllocRequest }}
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
    {{- else }}
  std::unique_ptr _write_bytes_boxed = std::make_unique<::fidl::internal::AlignedBuffer<_kWriteAllocSize>>();
  auto& _write_bytes_array = *_write_bytes_boxed;
    {{- end }}
  {{- end }}

  auto* _context = new ManagedResponseContext(std::move(_cb));
  auto status_and_error = {{ .Name }}({{- if .Request -}}
      _write_bytes_array.view(), {{ template "SyncClientMoveParams" .Request }}, {{ end }}_context);
  if (!status_and_error.ok()) {
    delete _context;
  }
  return status_and_error;
}
{{- end }}
`
