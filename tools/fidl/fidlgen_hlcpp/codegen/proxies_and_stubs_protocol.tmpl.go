// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const protocolTemplateProxiesAndStubs = `
{{- define "ProtocolForwardDeclaration/ProxiesAndStubs" }}
{{ EnsureNamespace . }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- range .DocComments }}
///{{ . }}
{{- end }}
using {{ .Name }}Ptr = ::fidl::InterfacePtr<{{ .Name }}>;
class {{ .ProxyName.Name }};
class {{ .StubName.Name }};
class {{ .EventSenderName.Name }};
class {{ .SyncName.Name }};
using {{ .Name }}SyncPtr = ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
class {{ .SyncProxyName.Name }};

namespace internal {

{{- range .Methods }}
constexpr uint64_t {{ .OrdinalName }} = {{ .Ordinal | printf "%#x" }}lu;
{{- end }}

}  // namespace
{{- PopNamespace }}
#endif  // __Fuchsia__
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
{{ .Name }}({{ template "Params" .Request }}{{ if .Request }}, {{ end }}{{ .CallbackType }} callback)
  {{- else -}}
{{ .Name }}({{ template "Params" .Request }})
  {{- end -}}
{{ end -}}

{{- define "EventMethodSignature" -}}
{{ .Name }}({{ template "Params" .Response }})
{{- end -}}

{{- define "SyncRequestMethodSignature" -}}
  {{- if .Response -}}
{{ .Name }}({{ template "Params" .Request }}{{ if .Request }}, {{ end }}{{ template "OutParams" .Response }})
  {{- else -}}
{{ .Name }}({{ template "Params" .Request }})
  {{- end -}}
{{ end -}}

{{- define "ProtocolDeclaration/ProxiesAndStubs" }}
#ifdef __Fuchsia__
{{- PushNamespace }}

{{- range .DocComments }}
///{{ . }}
{{- end }}
class {{ .Name }} {
 public:
  using Proxy_ = {{ .ProxyName }};
  using Stub_ = {{ .StubName }};
  using EventSender_ = {{ .EventSenderName }};
  using Sync_ = {{ .SyncName }};
  {{- if .ServiceName }}
  static const char Name_[];
  {{- end }}
  virtual ~{{ .Name }}();

  {{- range .Methods }}
    {{- if .HasResponse }}
  using {{ .CallbackType }} =
      {{ .CallbackWrapper }}<void({{ template "ParamTypes" .Response }})>;
    {{- end }}
    {{- if .HasRequest }}
      {{ range .DocComments }}
      ///{{ . }}
      {{- end }}
      {{- if .Transitional }}
  virtual void {{ template "RequestMethodSignature" . }} { }
      {{- else }}
  virtual void {{ template "RequestMethodSignature" . }} = 0;
      {{- end }}
    {{- end }}
  {{- end }}
};

class {{ .RequestDecoderName.Name }} {
 public:
  {{ .RequestDecoderName.Name }}() = default;
  virtual ~{{ .RequestDecoderName.Name }}() = default;
  static const fidl_type_t* GetType(uint64_t ordinal, bool* out_needs_response);

  {{- range .Methods }}
    {{- if .HasRequest }}
  virtual void {{ .Name }}({{ template "Params" .Request }}) = 0;
    {{- end }}
  {{- end }}
};

class {{ .ResponseDecoderName.Name }} {
 public:
  {{ .ResponseDecoderName.Name }}() = default;
  virtual ~{{ .ResponseDecoderName.Name }}() = default;
  static const fidl_type_t* GetType(uint64_t ordinal);

  {{- range .Methods }}
    {{- if .HasResponse }}
  virtual void {{ .Name }}({{ template "Params" .Response }}) = 0;
    {{- end }}
  {{- end }}
};

class {{ .EventSenderName.Name }} {
 public:
  virtual ~{{ .EventSenderName.Name }}();

  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- if .HasResponse }}
  virtual void {{ template "EventMethodSignature" . }} = 0;
      {{- end }}
    {{- end }}
  {{- end }}
};

class {{ .SyncName.Name }} {
 public:
  using Proxy_ = {{ .SyncProxyName }};
  virtual ~{{ .SyncName.Name }}();

  {{- range .Methods }}
    {{- if .HasRequest }}
  virtual zx_status_t {{ template "SyncRequestMethodSignature" . }} = 0;
    {{- end }}
  {{- end }}
};

class {{ .ProxyName.Name }} final : public ::fidl::internal::Proxy, public {{ .Name }} {
 public:
  explicit {{ .ProxyName.Name }}(::fidl::internal::ProxyController* controller);
  ~{{ .ProxyName.Name }}() override;

  zx_status_t Dispatch_(::fidl::HLCPPIncomingMessage message) override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }} override;
    {{- else if .HasResponse }}
  {{ .CallbackType }} {{ .Name }};
    {{- end }}
  {{- end }}

 private:
  {{ .ProxyName.Name }}(const {{ .ProxyName }}&) = delete;
  {{ .ProxyName.Name }}& operator=(const {{ .ProxyName }}&) = delete;

  ::fidl::internal::ProxyController* controller_;
};

class {{ .StubName.Name }} final : public ::fidl::internal::Stub, public {{ .EventSenderName }} {
 public:
  typedef class {{ . }} {{ .ClassName }};
  explicit {{ .StubName.Name }}({{ .ClassName }}* impl);
  ~{{ .StubName.Name }}() override;

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
  {{ .ClassName }}* impl_;
};

class {{ .SyncProxyName.Name }} : public {{ .SyncName }} {
 public:
  explicit {{ .SyncProxyName.Name }}(::zx::channel channel);
  ~{{ .SyncProxyName.Name }}() override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  zx_status_t {{ template "SyncRequestMethodSignature" . }} override;
    {{- end }}
  {{- end }}

  private:
  ::fidl::internal::SynchronousProxy proxy_;
  friend class ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
};

{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}

{{- define "ProtocolDefinition" }}
#ifdef __Fuchsia__
{{- PushNamespace }}

{{- range .Methods }}
{{ if .HasRequest }}
{{ EnsureNamespace .RequestCodingTable }}
extern "C" const fidl_type_t {{ .RequestCodingTable.Name }};
{{- end }}
{{- if .HasResponse }}
{{ EnsureNamespace .ResponseCodingTable }}
extern "C" const fidl_type_t {{ .ResponseCodingTable.Name }};
{{- end }}
{{- end }}

{{ EnsureNamespace . }}
{{ .Name }}::~{{ .Name }}() = default;

{{- if .ServiceName }}
const char {{ .Name }}::Name_[] = {{ .ServiceName }};
{{- end }}

const fidl_type_t* {{ .RequestDecoderName }}::GetType(uint64_t ordinal, bool* out_needs_response) {
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasRequest }}
    case internal::{{ .OrdinalName }}:
        {{- if .HasResponse }}
      *out_needs_response = true;
        {{- else }}
      *out_needs_response = false;
        {{- end }}
      return &{{ .RequestCodingTable }};
      {{- end }}
    {{- end }}
    default:
      *out_needs_response = false;
      return nullptr;
  }
}

const fidl_type_t* {{ .ResponseDecoderName.Name }}::GetType(uint64_t ordinal) {
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasResponse }}
    case internal::{{ .OrdinalName }}:
      return &{{ .ResponseCodingTable }};
      {{- end }}
    {{- end }}
    default:
      return nullptr;
  }
}

{{ .EventSenderName.Name }}::~{{ .EventSenderName.Name }}() = default;

{{ .SyncName.Name }}::~{{ .SyncName.Name }}() = default;

{{ .ProxyName.Name }}::{{ .ProxyName.Name }}(::fidl::internal::ProxyController* controller)
    : controller_(controller) {
  (void)controller_;
}

{{ .ProxyName.Name }}::~{{ .ProxyName.Name }}() = default;

zx_status_t {{ .ProxyName.Name }}::Dispatch_(::fidl::HLCPPIncomingMessage message) {
  zx_status_t status = ZX_OK;
  switch (message.ordinal()) {
    {{- range .Methods }}
      {{- if not .HasRequest }}
        {{- if .HasResponse }}
    case internal::{{ .OrdinalName }}:
    {
      if (!{{ .Name }}) {
        status = ZX_OK;
        break;
      }
      const char* error_msg = nullptr;
      status = message.Decode(&{{ .ResponseCodingTable }}, &error_msg);
      if (status != ZX_OK) {
        FIDL_REPORT_DECODING_ERROR(message, &{{ .ResponseCodingTable }}, error_msg);
        break;
      }
        {{- if .Response }}
      ::fidl::Decoder decoder(std::move(message));
        {{- end }}
      {{ .Name }}(
        {{- range $index, $param := .Response -}}
          {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type }}>(&decoder, {{ .Offset }})
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
      {{- if .Response }}
        ::fidl::Decoder decoder(std::move(message));
      {{- end }}
        callback_(
      {{- range $index, $param := .Response -}}
        {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type }}>(&decoder, {{ .Offset }})
      {{- end -}}
        );
        return ZX_OK;
      }, &{{ .ResponseCodingTable }});
}

}  // namespace
{{- end }}
void {{ $.ProxyName.Name }}::{{ template "RequestMethodSignature" . }} {
  ::fidl::Encoder _encoder(internal::{{ .OrdinalName }});
  controller_->Send(&{{ .RequestCodingTable }}, {{ $.RequestEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Request -}}
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

{{ .StubName.Name }}::{{ .StubName.Name }}({{ .ClassName }}* impl) : impl_(impl) {
  (void)impl_;
}

{{ .StubName.Name }}::~{{ .StubName.Name }}() = default;

namespace {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}

class {{ .ResponderType }} final {
 public:
  {{ .ResponderType }}(::fidl::internal::PendingResponse response)
      : response_(std::move(response)) {}

  void operator()({{ template "Params" .Response }}) {
    ::fidl::Encoder _encoder(internal::{{ .OrdinalName }});
    response_.Send(&{{ .ResponseCodingTable }}, {{ $.ResponseEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Response -}}
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

zx_status_t {{ .StubName.Name }}::Dispatch_(
    ::fidl::HLCPPIncomingMessage message,
    ::fidl::internal::PendingResponse response) {
  bool needs_response;
  const fidl_type_t* request_type = {{ .RequestDecoderName }}::GetType(message.ordinal(), &needs_response);
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
    case internal::{{ .OrdinalName }}:
    {
        {{- if .Request }}
      ::fidl::Decoder decoder(std::move(message));
        {{- end }}
      impl_->{{ .Name }}(
        {{- range $index, $param := .Request -}}
          {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type }}>(&decoder, {{ .Offset }})
        {{- end -}}
        {{- if .HasResponse -}}
          {{- if .Request }}, {{ end -}}{{ .ResponderType }}(std::move(response))
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
void {{ $.StubName.Name }}::{{ template "EventMethodSignature" . }} {
  ::fidl::Encoder _encoder(internal::{{ .OrdinalName }});
  sender_()->Send(&{{ .ResponseCodingTable }}, {{ $.ResponseEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Response -}}
    , &{{ $param.Name }}
  {{- end -}}
  ));
}
    {{- end }}
  {{- end }}
{{- end }}

{{ .SyncProxyName.Name }}::{{ .SyncProxyName.Name }}(::zx::channel channel)
    : proxy_(::std::move(channel)) {}

{{ .SyncProxyName.Name }}::~{{ .SyncProxyName.Name }}() = default;

{{- range .Methods }}
  {{- if .HasRequest }}

zx_status_t {{ $.SyncProxyName.Name }}::{{ template "SyncRequestMethodSignature" . }} {
  ::fidl::Encoder _encoder(internal::{{ .OrdinalName }});
    {{- if .HasResponse }}
  ::fidl::IncomingMessageBuffer buffer_;
  ::fidl::HLCPPIncomingMessage response_ = buffer_.CreateEmptyIncomingMessage();
  zx_status_t status_ = proxy_.Call(&{{ .RequestCodingTable }}, &{{ .ResponseCodingTable }}, {{ $.RequestEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Request -}}
    , &{{ $param.Name }}
  {{- end -}}
  ), &response_);
  if (status_ != ZX_OK)
    return status_;
      {{- if .Response }}
  ::fidl::Decoder decoder_(std::move(response_));
        {{- range $index, $param := .Response }}
  *out_{{ .Name }} = ::fidl::DecodeAs<{{ .Type }}>(&decoder_, {{ .Offset }});
        {{- end }}
      {{- end }}
  return ZX_OK;
    {{- else }}
  return proxy_.Send(&{{ .RequestCodingTable }}, {{ $.RequestEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Request -}}
    , &{{ $param.Name }}
  {{- end -}}
  ));
    {{- end }}
}
  {{- end }}
{{- end }}

{{- PopNamespace }}
#endif // __Fuchsia__
{{ end }}

{{- define "ProtocolTestBase" }}
{{ EnsureNamespace .TestBase }}
class {{ .TestBase.Name }} : public {{ . }} {
  public:
  virtual ~{{ .TestBase.Name }}() { }
  virtual void NotImplemented_(const std::string& name) = 0;

  {{- range .Methods }}
    {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }} override {
    NotImplemented_("{{ .Name }}");
  }
    {{- end }}
  {{- end }}

};
{{ end }}
`
