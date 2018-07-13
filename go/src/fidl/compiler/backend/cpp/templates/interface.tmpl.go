// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceForwardDeclaration" }}
class {{ .Name }};
using {{ .Name }}Ptr = ::fidl::InterfacePtr<{{ .Name }}>;
class {{ .ProxyName }};
class {{ .StubName }};
class {{ .EventSenderName }};
class {{ .SyncName }};
class {{ .Sync2Name }};
using {{ .Name }}SyncPtr = ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
using {{ .Name }}Sync2Ptr = ::fidl::Synchronous2InterfacePtr<{{ .Name }}>;
class {{ .SyncProxyName }};
class {{ .Sync2ProxyName }};
{{- end }}

{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "OutParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }}* out_{{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "ParamTypes" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }}
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

{{- define "InterfaceDeclaration" }}
class {{ .Name }} {
 public:
  using Proxy_ = {{ .ProxyName }};
  using Stub_ = {{ .StubName }};
  using EventSender_ = {{ .EventSenderName }};
  using Sync_ = {{ .SyncName }};
  using Sync2_ = {{ .Sync2Name }};
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
  virtual void {{ template "RequestMethodSignature" . }} = 0;
    {{- end }}
  {{- end }}
};

class {{ .EventSenderName }} {
 public:
  virtual ~{{ .EventSenderName }}();

  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- if .HasResponse }}
  virtual void {{ template "EventMethodSignature" . }} = 0;
      {{- end }}
    {{- end }}
  {{- end }}
};

class {{ .SyncName }} {
 public:
  using Proxy_ = {{ .SyncProxyName }};
  virtual ~{{ .SyncName }}();

  {{- range .Methods }}
    {{- if .HasRequest }}
  virtual zx_status_t {{ template "SyncRequestMethodSignature" . }} = 0;
    {{- end }}
  {{- end }}
};

class {{ .Sync2Name }} {
  public:
   using Proxy_ = {{ .Sync2ProxyName }};
   virtual ~{{ .Sync2Name }}();

   {{- range .Methods }}
     {{- if .HasRequest }}
   virtual ::fidl::Sync2Status {{ template "SyncRequestMethodSignature" . }} = 0;
     {{- end }}
   {{- end }}
 };

class {{ .ProxyName }} : public ::fidl::internal::Proxy, public {{ .Name }} {
 public:
  explicit {{ .ProxyName }}(::fidl::internal::ProxyController* controller);
  ~{{ .ProxyName }}() override;

  zx_status_t Dispatch_(::fidl::Message message) override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }} override;
    {{- else if .HasResponse }}
  {{ .CallbackType }} {{ .Name }};
    {{- end }}
  {{- end }}

 private:
  {{ .ProxyName }}(const {{ .ProxyName }}&) = delete;
  {{ .ProxyName }}& operator=(const {{ .ProxyName }}&) = delete;

  ::fidl::internal::ProxyController* controller_;
};

class {{ .StubName }} : public ::fidl::internal::Stub, public {{ .EventSenderName }} {
 public:
  explicit {{ .StubName }}({{ .Name }}* impl);
  ~{{ .StubName }}() override;

  zx_status_t Dispatch_(::fidl::Message message,
                        ::fidl::internal::PendingResponse response) override;

  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- if .HasResponse }}
  void {{ template "EventMethodSignature" . }} override;
      {{- end }}
    {{- end }}
  {{- end }}

 private:
  {{ .Name }}* impl_;
};

class {{ .SyncProxyName }} : public {{ .SyncName }} {
 public:
  explicit {{ .SyncProxyName }}(::zx::channel channel);
  ~{{ .SyncProxyName }}() override;

  ::fidl::internal::SynchronousProxy& proxy() { return proxy_; }

  {{- range .Methods }}
    {{- if .HasRequest }}
  zx_status_t {{ template "SyncRequestMethodSignature" . }} override;
    {{- end }}
  {{- end }}

  private:
  ::fidl::internal::SynchronousProxy proxy_;
};

class {{ .Sync2ProxyName }} : public {{ .Sync2Name }} {
 public:
  explicit {{ .Sync2ProxyName }}(::zx::channel channel);
  ~{{ .Sync2ProxyName }}() override;

  ::fidl::internal::SynchronousProxy& proxy() { return proxy_; }

  {{- range .Methods }}
    {{- if .HasRequest }}
  ::fidl::Sync2Status {{ template "SyncRequestMethodSignature" . }} override;
    {{- end }}
  {{- end }}

 private:
  ::fidl::internal::SynchronousProxy proxy_;
 };
{{- end }}

{{- define "InterfaceDefinition" }}
namespace {

{{ range .Methods }}
constexpr uint32_t {{ .OrdinalName }} = {{ .Ordinal }}u;
  {{- if .HasRequest }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
  {{- end }}
  {{- if .HasResponse }}
extern "C" const fidl_type_t {{ .ResponseTypeName }};
  {{- end }}
{{- end }}

}  // namespace

{{ .Name }}::~{{ .Name }}() = default;

{{- if .ServiceName }}
const char {{ .Name }}::Name_[] = {{ .ServiceName }};
{{- end }}

{{ .EventSenderName }}::~{{ .EventSenderName }}() = default;

{{ .SyncName }}::~{{ .SyncName }}() = default;

{{ .Sync2Name }}::~{{ .Sync2Name }}() = default;

{{ .ProxyName }}::{{ .ProxyName }}(::fidl::internal::ProxyController* controller)
    : controller_(controller) {
  (void) controller_;
}

{{ .ProxyName }}::~{{ .ProxyName }}() = default;

zx_status_t {{ .ProxyName }}::Dispatch_(::fidl::Message message) {
  zx_status_t status = ZX_OK;
  switch (message.ordinal()) {
    {{- range .Methods }}
      {{- if not .HasRequest }}
        {{- if .HasResponse }}
    case {{ .OrdinalName }}: {
      if (!{{ .Name }}) {
        status = ZX_OK;
        break;
      }
      const char* error_msg = nullptr;
      status = message.Decode(&{{ .ResponseTypeName }}, &error_msg);
      if (status != ZX_OK) {
        FIDL_REPORT_DECODING_ERROR(message, &{{ .ResponseTypeName }}, error_msg);
        break;
      }
        {{- if .Response }}
      ::fidl::Decoder decoder(std::move(message));
          {{- range $index, $param := .Response }}
      auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .Offset }});
          {{- end }}
        {{- end }}
      {{ .Name }}(
        {{- range $index, $param := .Response -}}
          {{- if $index }}, {{ end }}std::move(arg{{ $index }})
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

class {{ .ResponseHandlerType }} : public ::fidl::internal::MessageHandler {
  public:
  {{ .ResponseHandlerType }}({{ $.Name }}::{{ .CallbackType }} callback)
      : callback_(std::move(callback)) {
    ZX_DEBUG_ASSERT_MSG(callback_,
                        "Callback must not be empty for {{ $.Name }}::{{ .Name }}\n");
  }

  zx_status_t OnMessage(::fidl::Message message) override {
    const char* error_msg = nullptr;
    zx_status_t status = message.Decode(&{{ .ResponseTypeName }}, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_DECODING_ERROR(message, &{{ .ResponseTypeName }}, error_msg);
      return status;
    }
      {{- if .Response }}
    ::fidl::Decoder decoder(std::move(message));
        {{- range $index, $param := .Response }}
    auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .Offset }});
        {{- end }}
      {{- end }}
    callback_(
      {{- range $index, $param := .Response -}}
        {{- if $index }}, {{ end }}std::move(arg{{ $index }})
      {{- end -}}
    );
    return ZX_OK;
  }

  private:
  {{ $.Name }}::{{ .CallbackType }} callback_;

  {{ .ResponseHandlerType }}(const {{ .ResponseHandlerType }}&) = delete;
  {{ .ResponseHandlerType }}& operator=(const {{ .ResponseHandlerType }}&) = delete;
};

}  // namespace

{{- end }}
void {{ $.ProxyName }}::{{ template "RequestMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
    {{- if .Request }}
  _encoder.Alloc({{ .RequestSize }} - sizeof(fidl_message_header_t));
      {{- range .Request }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
      {{- end }}
    {{- end }}
    {{- if .HasResponse }}
  controller_->Send(&{{ .RequestTypeName }}, _encoder.GetMessage(), std::make_unique<{{ .ResponseHandlerType }}>(std::move(callback)));
    {{- else }}
  controller_->Send(&{{ .RequestTypeName }}, _encoder.GetMessage(), nullptr);
    {{- end }}
}
  {{- end }}
{{- end }}

{{ .StubName }}::{{ .StubName }}({{ .Name }}* impl) : impl_(impl) {
  (void) impl_;
}

{{ .StubName }}::~{{ .StubName }}() = default;

namespace {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}

class {{ .ResponderType }} {
 public:
 {{ .ResponderType }}(::fidl::internal::PendingResponse response)
      : response_(std::move(response)) {}

  void operator()({{ template "Params" .Response }}) {
    ::fidl::Encoder _encoder({{ .OrdinalName }});
      {{- if .Response }}
  _encoder.Alloc({{ .ResponseSize }} - sizeof(fidl_message_header_t));
        {{- range .Response }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
        {{- end }}
      {{- end }}
    response_.Send(&{{ .ResponseTypeName }}, _encoder.GetMessage());
  }

 private:
  ::fidl::internal::PendingResponse response_;
};
    {{- end }}
  {{- end }}
{{- end }}

}  // namespace

zx_status_t {{ .StubName }}::Dispatch_(
    ::fidl::Message message,
    ::fidl::internal::PendingResponse response) {
  zx_status_t status = ZX_OK;
  switch (message.ordinal()) {
    {{- range .Methods }}
      {{- if .HasRequest }}
    case {{ .OrdinalName }}: {
      const char* error_msg = nullptr;
      status = message.Decode(&{{ .RequestTypeName }}, &error_msg);
      if (status != ZX_OK) {
        FIDL_REPORT_DECODING_ERROR(message, &{{ .RequestTypeName }}, error_msg);
        break;
      }
        {{- if .Request }}
      ::fidl::Decoder decoder(std::move(message));
          {{- range $index, $param := .Request }}
      auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .Offset }});
          {{- end }}
        {{- end }}
      impl_->{{ .Name }}(
        {{- range $index, $param := .Request -}}
          {{- if $index }}, {{ end }}std::move(arg{{ $index }})
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
void {{ $.StubName }}::{{ template "EventMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
    {{- if .Response }}
  _encoder.Alloc({{ .ResponseSize }} - sizeof(fidl_message_header_t));
      {{- range .Response }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
      {{- end }}
    {{- end }}
  controller()->Send(&{{ .ResponseTypeName }}, _encoder.GetMessage());
}
    {{- end }}
  {{- end }}
{{- end }}

{{ .SyncProxyName }}::{{ .SyncProxyName }}(::zx::channel channel)
  : proxy_(::std::move(channel)) {}

{{ .SyncProxyName }}::~{{ .SyncProxyName }}() = default;

{{- range .Methods }}
  {{- if .HasRequest }}
zx_status_t {{ $.SyncProxyName }}::{{ template "SyncRequestMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
    {{- if .Request }}
  _encoder.Alloc({{ .RequestSize }} - sizeof(fidl_message_header_t));
      {{- range .Request }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
      {{- end }}
    {{- end }}
    {{- if .HasResponse }}
  ::fidl::MessageBuffer buffer_;
  ::fidl::Message response_ = buffer_.CreateEmptyMessage();
  zx_status_t status_ = proxy_.Call(&{{ .RequestTypeName }}, &{{ .ResponseTypeName }}, _encoder.GetMessage(), &response_);
  if (status_ != ZX_OK)
    return status_;
      {{- if .Response }}
  ::fidl::Decoder decoder_(std::move(response_));
        {{- range $index, $param := .Response }}
  *out_{{ .Name }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder_, {{ .Offset }});
        {{- end }}
      {{- end }}
  return ZX_OK;
    {{- else }}
  return proxy_.Send(&{{ .RequestTypeName }}, _encoder.GetMessage());
    {{- end }}
}
  {{- end }}
{{- end }}

{{ .Sync2ProxyName }}::{{ .Sync2ProxyName }}(::zx::channel channel)
  : proxy_(::std::move(channel)) {}

{{ .Sync2ProxyName }}::~{{ .Sync2ProxyName }}() = default;

{{- range .Methods }}
  {{- if .HasRequest }}
::fidl::Sync2Status {{ $.Sync2ProxyName }}::{{ template "SyncRequestMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
    {{- if .Request }}
  _encoder.Alloc({{ .RequestSize }} - sizeof(fidl_message_header_t));
      {{- range .Request }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
      {{- end }}
    {{- end }}
    {{- if .HasResponse }}
  ::fidl::MessageBuffer buffer_;
  ::fidl::Message response_ = buffer_.CreateEmptyMessage();
  zx_status_t status_ = proxy_.Call(&{{ .RequestTypeName }}, &{{ .ResponseTypeName }}, _encoder.GetMessage(), &response_);
  if (status_ != ZX_OK)
    return ::fidl::Sync2Status(status_);
      {{- if .Response }}
  ::fidl::Decoder decoder_(std::move(response_));
        {{- range $index, $param := .Response }}
  *out_{{ .Name }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder_, {{ .Offset }});
        {{- end }}
      {{- end }}
  return ::fidl::Sync2Status(ZX_OK);;
    {{- else }}
  return ::fidl::Sync2Status(proxy_.Send(&{{ .RequestTypeName }}, _encoder.GetMessage()));
    {{- end }}
}
  {{- end }}
{{- end }}

{{ end }}
`
