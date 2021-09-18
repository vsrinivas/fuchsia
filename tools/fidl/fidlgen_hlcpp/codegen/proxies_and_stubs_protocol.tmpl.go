// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const protocolTemplateProxiesAndStubs = `
{{- define "ProtocolForwardDeclaration/ProxiesAndStubs" }}
{{ EnsureNamespace . }}
{{- IfdefFuchsia -}}
{{- .Docs }}
using {{ .Name }}Ptr = ::fidl::InterfacePtr<{{ .Name }}>;
class {{ .Proxy.Name }};
class {{ .Stub.Name }};
class {{ .EventSender.Name }};
class {{ .SyncInterface.Name }};
using {{ .Name }}SyncPtr = ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
class {{ .SyncProxy.Name }};

{{ range .Methods }}
{{- EnsureNamespace .OrdinalName }}
constexpr uint64_t {{ .OrdinalName.Name }} = {{ .Ordinal | printf "%#x" }}lu;
{{- end }}

{{- EndifFuchsia -}}
{{- end }}

{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "OutParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type }}* out_{{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "ParamTypes" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type }}
  {{- end -}}
{{ end }}

{{- define "RequestMethodSignature" -}}
  {{- if .HasResponse -}}
{{ .Name }}({{ template "Params" .RequestArgs }}{{ if .RequestArgs }}, {{ end }}{{ .CallbackType }} callback)
  {{- else -}}
{{ .Name }}({{ template "Params" .RequestArgs }})
  {{- end -}}
{{ end -}}

{{- define "EventMethodSignature" -}}
{{ .Name }}({{ template "Params" .ResponseArgs }})
{{- end -}}

{{- define "SyncRequestMethodSignature" -}}
  {{- if .ResponseArgs -}}
{{ .Name }}({{ template "Params" .RequestArgs }}{{ if .RequestArgs }}, {{ end }}{{ template "OutParams" .ResponseArgs }})
  {{- else -}}
{{ .Name }}({{ template "Params" .RequestArgs }})
  {{- end -}}
{{ end -}}

{{- define "ProtocolDeclaration/ProxiesAndStubs" }}
{{- IfdefFuchsia -}}

{{- .Docs }}
class {{ .Name }} {
 public:
  using Proxy_ = {{ .Proxy }};
  using Stub_ = {{ .Stub }};
  using EventSender_ = {{ .EventSender }};
  using Sync_ = {{ .SyncInterface }};
  {{- if .DiscoverableName }}
  static const char Name_[];
  {{- end }}
  virtual ~{{ .Name }}();

  {{- range .Methods }}
    {{- if .HasResponse }}
  using {{ .CallbackType }} =
      {{ .CallbackWrapper }}<void({{ template "ParamTypes" .ResponseArgs }})>;
    {{- end }}
    {{- if .HasRequest }}
      {{ .Docs }}
      {{- if .Transitional }}
  virtual void {{ template "RequestMethodSignature" . }} { }
      {{- else }}
  virtual void {{ template "RequestMethodSignature" . }} = 0;
      {{- end }}
    {{- end }}
  {{- end }}
};

class {{ .RequestDecoder.Name }} {
 public:
  {{ .RequestDecoder.Name }}() = default;
  virtual ~{{ .RequestDecoder.Name }}() = default;
  static const fidl_type_t* GetType(uint64_t ordinal, bool* out_needs_response);

  {{- range .Methods }}
    {{- if .HasRequest }}
  virtual void {{ .Name }}({{ template "Params" .RequestArgs }}) = 0;
    {{- end }}
  {{- end }}
};

class {{ .ResponseDecoder.Name }} {
 public:
  {{ .ResponseDecoder.Name }}() = default;
  virtual ~{{ .ResponseDecoder.Name }}() = default;
  static const fidl_type_t* GetType(uint64_t ordinal);

  {{- range .Methods }}
    {{- if .HasResponse }}
  virtual void {{ .Name }}({{ template "Params" .ResponseArgs }}) = 0;
    {{- end }}
  {{- end }}
};

class {{ .EventSender.Name }} {
 public:
  virtual ~{{ .EventSender.Name }}();

  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- if .HasResponse }}
  virtual void {{ template "EventMethodSignature" . }} = 0;
      {{- end }}
    {{- end }}
  {{- end }}
};

class {{ .SyncInterface.Name }} {
 public:
  using Proxy_ = {{ .SyncProxy }};
  virtual ~{{ .SyncInterface.Name }}();

  {{- range .Methods }}
    {{- if .HasRequest }}
  virtual zx_status_t {{ template "SyncRequestMethodSignature" . }} = 0;
    {{- end }}
  {{- end }}
};

class {{ .Proxy.Name }} final : public ::fidl::internal::Proxy, public {{ .Name }} {
 public:
  explicit {{ .Proxy.Name }}(::fidl::internal::ProxyController* controller);
  ~{{ .Proxy.Name }}() override;

  zx_status_t Dispatch_(::fidl::HLCPPIncomingMessage message) override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  {{ .CtsMethodAnnotation }}
  void {{ template "RequestMethodSignature" . }} override;
    {{- else if .HasResponse }}
  {{ .CallbackType }} {{ .Name }};
    {{- end }}
  {{- end }}

 private:
  {{ .Proxy.Name }}(const {{ .Proxy }}&) = delete;
  {{ .Proxy.Name }}& operator=(const {{ .Proxy }}&) = delete;

  ::fidl::internal::ProxyController* controller_;
};

class {{ .Stub.Name }} final : public ::fidl::internal::Stub, public {{ .EventSender }} {
 public:
  typedef class {{ . }} {{ .InterfaceAliasForStub.Self }};
  explicit {{ .Stub.Name }}({{ .InterfaceAliasForStub }}* impl);
  ~{{ .Stub.Name }}() override;

  zx_status_t Dispatch_(::fidl::HLCPPIncomingMessage message,
                        ::fidl::internal::PendingResponse response) override;

  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- if .HasResponse }}
  void {{ template "EventMethodSignature" . }} override;
      {{- end }}
    {{- end }}
  {{- end }}

 private:
  {{ .InterfaceAliasForStub }}* impl_;
};

class {{ .SyncProxy.Name }} : public {{ .SyncInterface }} {
 public:
  explicit {{ .SyncProxy.Name }}(::zx::channel channel);
  ~{{ .SyncProxy.Name }}() override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  {{ .CtsMethodAnnotation }}
  zx_status_t {{ template "SyncRequestMethodSignature" . }} override;
    {{- end }}
  {{- end }}

  private:
  ::fidl::internal::SynchronousProxy proxy_;
  friend class ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
};

{{- EndifFuchsia -}}
{{- end }}

{{- define "ProtocolDefinition" }}
{{- IfdefFuchsia -}}

{{- range .Methods }}
{{ if .HasRequest }}
{{ EnsureNamespace .Request.HlCodingTable }}
__LOCAL extern "C" const fidl_type_t {{ .Request.HlCodingTable.Name }};
{{- end }}
{{- if .HasResponse }}
{{ EnsureNamespace .Response.HlCodingTable }}
__LOCAL extern "C" const fidl_type_t {{ .Response.HlCodingTable.Name }};
{{- end }}
{{- end }}

{{ EnsureNamespace . }}
{{ .Name }}::~{{ .Name }}() = default;

{{- if .DiscoverableName }}
const char {{ .Name }}::Name_[] = {{ .DiscoverableName }};
{{- end }}

const fidl_type_t* {{ .RequestDecoder }}::GetType(uint64_t ordinal, bool* out_needs_response) {
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasRequest }}
    case {{ .OrdinalName }}:
        {{- if .HasResponse }}
      *out_needs_response = true;
        {{- else }}
      *out_needs_response = false;
        {{- end }}
      return &{{ .Request.HlCodingTable }};
      {{- end }}
    {{- end }}
    default:
      *out_needs_response = false;
      return nullptr;
  }
}

const fidl_type_t* {{ .ResponseDecoder.Name }}::GetType(uint64_t ordinal) {
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasResponse }}
    case {{ .OrdinalName }}:
      return &{{ .Response.HlCodingTable }};
      {{- end }}
    {{- end }}
    default:
      return nullptr;
  }
}

{{ .EventSender.Name }}::~{{ .EventSender.Name }}() = default;

{{ .SyncInterface.Name }}::~{{ .SyncInterface.Name }}() = default;

{{ .Proxy.Name }}::{{ .Proxy.Name }}(::fidl::internal::ProxyController* controller)
    : controller_(controller) {
  (void)controller_;
}

{{ .Proxy.Name }}::~{{ .Proxy.Name }}() = default;

zx_status_t {{ .Proxy.Name }}::Dispatch_(::fidl::HLCPPIncomingMessage message) {
  zx_status_t status = ZX_OK;
  switch (message.ordinal()) {
    {{- range .Methods }}
      {{- if not .HasRequest }}
        {{- if .HasResponse }}
    case {{ .OrdinalName }}:
    {
      if (!{{ .Name }}) {
        status = ZX_OK;
        break;
      }
      const char* error_msg = nullptr;
      status = message.Decode(&{{ .Response.HlCodingTable }}, &error_msg);
      if (status != ZX_OK) {
        FIDL_REPORT_DECODING_ERROR(message, &{{ .Response.HlCodingTable }}, error_msg);
        break;
      }
        {{- if .ResponseArgs }}
      ::fidl::Decoder decoder(std::move(message));
        {{- end }}
      {{ .Name }}(
        {{- range $index, $param := .ResponseArgs -}}
          {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type }}>(&decoder, {{ .OffsetV2 }})
        {{- end -}}
      );
      break;
    }
        {{- end }}
      {{- end }}
    {{- end }}
    default: {
      status = ZX_ERR_NOT_SUPPORTED;
      break;
    }
  }
  return status;
}

{{ range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
namespace {

::std::unique_ptr<::fidl::internal::SingleUseMessageHandler>
{{- /* Note: fidl::internal::SingleUseMessageHandler assumes that the lambda captures a single */}}
{{- /* fit::function. When changing CallbackType, make sure to update SingleUseMessageHandler. */}}
{{ .ResponseHandlerType }}({{ $.Name }}::{{ .CallbackType }}&& callback) {
  ZX_DEBUG_ASSERT_MSG(callback,
                      "Callback must not be empty for {{ $.Name }}::{{ .Name }}\n");
  return ::std::make_unique<::fidl::internal::SingleUseMessageHandler>(
      [callback_ = std::move(callback)](::fidl::HLCPPIncomingMessage&& message) {
      {{- if .ResponseArgs }}
        ::fidl::Decoder decoder(std::move(message));
      {{- end }}
        callback_(
      {{- range $index, $param := .ResponseArgs -}}
        {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type }}>(&decoder, {{ .OffsetV2 }})
      {{- end -}}
        );
        return ZX_OK;
      }, &{{ .Response.HlCodingTable }});
}

}  // namespace
{{- end }}
void {{ $.Proxy.Name }}::{{ template "RequestMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
  controller_->Send(&{{ .Request.HlCodingTable }}, {{ $.RequestEncoder }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .RequestArgs -}}
    , &{{ $param.Name }}
  {{- end -}}
  )
  {{- if .HasResponse -}}
    , {{ .ResponseHandlerType }}(std::move(callback))
  {{- else -}}
    , nullptr
  {{- end -}}
  );
}
  {{- end }}
{{- end }}

{{ .Stub.Name }}::{{ .Stub.Name }}({{ .InterfaceAliasForStub }}* impl) : impl_(impl) {
  (void)impl_;
}

{{ .Stub.Name }}::~{{ .Stub.Name }}() = default;

namespace {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}

class {{ .ResponderType }} final {
 public:
  {{ .ResponderType }}(::fidl::internal::PendingResponse response)
      : response_(std::move(response)) {}

  void operator()({{ template "Params" .ResponseArgs }}) {
    ::fidl::Encoder _encoder({{ .OrdinalName }});
    response_.Send(&{{ .Response.HlCodingTable }}, {{ $.ResponseEncoder }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .ResponseArgs -}}
    , &{{ $param.Name }}
  {{- end -}}
  ));
  }

 private:
  ::fidl::internal::PendingResponse response_;
};
    {{- end }}
  {{- end }}
{{- end }}

}  // namespace

zx_status_t {{ .Stub.Name }}::Dispatch_(
    ::fidl::HLCPPIncomingMessage message,
    ::fidl::internal::PendingResponse response) {
  bool needs_response;
  const fidl_type_t* request_type = {{ .RequestDecoder }}::GetType(message.ordinal(), &needs_response);
  if (request_type == nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (response.needs_response() != needs_response) {
    if (needs_response) {
      FIDL_REPORT_DECODING_ERROR(message, request_type, "Message needing a response with no txid");
    } else {
      FIDL_REPORT_DECODING_ERROR(message, request_type, "Message not needing a response with a txid");
    }
    return ZX_ERR_INVALID_ARGS;
  }
  const char* error_msg = nullptr;
  zx_status_t status = message.Decode(request_type, &error_msg);
  if (status != ZX_OK) {
    FIDL_REPORT_DECODING_ERROR(message, request_type, error_msg);
    return status;
  }
  uint64_t ordinal = message.ordinal();
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasRequest }}
    case {{ .OrdinalName }}:
    {
        {{- if .RequestArgs }}
      ::fidl::Decoder decoder(std::move(message));
        {{- end }}
      impl_->{{ .Name }}(
        {{- range $index, $param := .RequestArgs -}}
          {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type }}>(&decoder, {{ .OffsetV2 }})
        {{- end -}}
        {{- if .HasResponse -}}
          {{- if .RequestArgs }}, {{ end -}}{{ .ResponderType }}(std::move(response))
        {{- end -}}
      );
      break;
    }
      {{- end }}
    {{- end }}
    default: {
      status = ZX_ERR_NOT_SUPPORTED;
      break;
    }
  }
  return status;
}

{{- range .Methods }}
  {{- if not .HasRequest }}
    {{- if .HasResponse }}
void {{ $.Stub.Name }}::{{ template "EventMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
  sender_()->Send(&{{ .Response.HlCodingTable }}, {{ $.ResponseEncoder }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .ResponseArgs -}}
    , &{{ $param.Name }}
  {{- end -}}
  ));
}
    {{- end }}
  {{- end }}
{{- end }}

{{ .SyncProxy.Name }}::{{ .SyncProxy.Name }}(::zx::channel channel)
    : proxy_(::std::move(channel)) {}

{{ .SyncProxy.Name }}::~{{ .SyncProxy.Name }}() = default;

{{- range .Methods }}
  {{- if .HasRequest }}

zx_status_t {{ $.SyncProxy.Name }}::{{ template "SyncRequestMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
    {{- if .HasResponse }}
  ::fidl::IncomingMessageBuffer buffer_;
  ::fidl::HLCPPIncomingMessage response_ = buffer_.CreateEmptyIncomingMessage();
  zx_status_t status_ = proxy_.Call(&{{ .Request.HlCodingTable }}, &{{ .Response.HlCodingTable }}, {{ $.RequestEncoder }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .RequestArgs -}}
    , &{{ $param.Name }}
  {{- end -}}
  ), &response_);
  if (status_ != ZX_OK)
    return status_;
      {{- if .ResponseArgs }}
  ::fidl::Decoder decoder_(std::move(response_));
        {{- range $index, $param := .ResponseArgs }}
  *out_{{ .Name }} = ::fidl::DecodeAs<{{ .Type }}>(&decoder_, {{ .OffsetV2 }});
        {{- end }}
      {{- end }}
  return ZX_OK;
    {{- else }}
  return proxy_.Send(&{{ .Request.HlCodingTable }}, {{ $.RequestEncoder }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .RequestArgs -}}
    , &{{ $param.Name }}
  {{- end -}}
  ));
    {{- end }}
}
  {{- end }}
{{- end }}

{{- EndifFuchsia -}}
{{ end }}
`
