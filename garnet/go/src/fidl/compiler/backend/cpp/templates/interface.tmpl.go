// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceForwardDeclaration" }}
#ifdef __Fuchsia__
class {{ .Name }};
using {{ .Name }}Ptr = ::fidl::InterfacePtr<{{ .Name }}>;
class {{ .ProxyName }};
class {{ .StubName }};
class {{ .EventSenderName }};
class {{ .SyncName }};
using {{ .Name }}SyncPtr = ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
class {{ .SyncProxyName }};
namespace internal {

{{- range .Methods }}
  {{- range .Ordinals.Reads }}
constexpr uint64_t {{ .Name }} = {{ .Ordinal }}lu;
  {{- end }}
{{- end }}

}  // namespace
#endif // __Fuchsia__
{{- end }}

{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "PointerParams" -}}
  {{- range $index, $param := . -}}
    , {{ $param.Type.Decl }}* {{ $param.Name }}
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
#ifdef __Fuchsia__
{{range .DocComments}}
//{{ . }}
{{- end}}
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
  //{{ . }}
      {{- end }}
      {{- if .Transitional }}
  virtual void {{ template "RequestMethodSignature" . }} { }
      {{- else }}
  virtual void {{ template "RequestMethodSignature" . }} = 0;
      {{- end }}
    {{- end }}
  {{- end }}
};

class {{ .RequestEncoderName }} {
 public:
  {{- range .Methods }}
    {{- if .HasRequest }}
  static ::fidl::Message {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .Request }}) {
    _encoder->Alloc({{ .RequestSizeV1NoEE }} - sizeof(fidl_message_header_t));
    {{- range .Request }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV1 }});
    {{- end }}
    return _encoder->GetMessage();
  }
    {{- end }}
  {{- end }}
};

class {{ .RequestDecoderName }} {
 public:
  {{ .RequestDecoderName }}() = default;
  virtual ~{{ .RequestDecoderName }}() = default;
  static const fidl_type_t* GetType(uint64_t ordinal, bool* out_needs_response);
  zx_status_t Decode_(::fidl::Message request) {
    bool needs_response;
    const fidl_type_t* request_type = GetType(request.ordinal(), &needs_response);
    if (request_type == nullptr) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    const char* error_msg = nullptr;
    zx_status_t status = request.Decode(request_type, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_DECODING_ERROR(request, request_type, error_msg);
      return status;
    }
    ::fidl::Decoder request_decoder(std::move(request));
    switch (request.ordinal()) {
      {{- range .Methods }}
        {{- if .HasRequest }}
          {{- range .Ordinals.Reads }}
      case internal::{{ .Name }}:
          {{- end }}
      {
          {{- if .Request }}
            {{- range $index, $param := .Request }}
        auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&request_decoder, {{ .OffsetV1 }});
            {{- end }}
          {{- end }}
        {{ .Name }}(
          {{- range $index, $param := .Request -}}
            {{- if $index }}, {{ end }}std::move(arg{{ $index }})
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
    {{- if .HasRequest }}
  virtual void {{ .Name }}({{ template "Params" .Request }}) = 0;
    {{- end }}
  {{- end }}
};

class {{ .ResponseEncoderName }} {
 public:
  {{- range .Methods }}
    {{- if .HasResponse }}
  static ::fidl::Message {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .Response }}) {
    _encoder->Alloc({{ .ResponseSizeV1NoEE }} - sizeof(fidl_message_header_t));
    {{- range .Response }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV1 }});
    {{- end }}
    return _encoder->GetMessage();
  }
    {{- end }}
  {{- end }}
};

class {{ .ResponseDecoderName }} {
 public:
  {{ .ResponseDecoderName }}() = default;
  virtual ~{{ .ResponseDecoderName }}() = default;
  static const fidl_type_t* GetType(uint64_t ordinal);
  zx_status_t Decode_(::fidl::Message response) {
    const fidl_type_t* response_type = GetType(response.ordinal());
    if (response_type == nullptr) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    const char* error_msg = nullptr;
    zx_status_t status = response.Decode(response_type, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_DECODING_ERROR(response, response_type, error_msg);
      return status;
    }
    ::fidl::Decoder response_decoder(std::move(response));
    switch (response.ordinal()) {
      {{- range .Methods }}
        {{- if .HasResponse }}
          {{- range .Ordinals.Reads }}
      case internal::{{ .Name }}:
          {{- end }}
      {
            {{- range $index, $param := .Response }}
        auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&response_decoder, {{ .OffsetV1 }});
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
      default: {
        break;
      }
    }
    return ZX_OK;
  }

  {{- range .Methods }}
    {{- if .HasResponse }}
  virtual void {{ .Name }}({{ template "Params" .Response }}) = 0;
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

class {{ .ProxyName }} final : public ::fidl::internal::Proxy, public {{ .Name }} {
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

class {{ .StubName }} final : public ::fidl::internal::Stub, public {{ .EventSenderName }} {
 public:
  typedef class {{ .Namespace }}::{{ .Name }} {{ .ClassName }};
  explicit {{ .StubName }}({{ .ClassName }}* impl);
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
  {{ .ClassName }}* impl_;
};

class {{ .SyncProxyName }} : public {{ .SyncName }} {
 public:
  explicit {{ .SyncProxyName }}(::zx::channel channel);
  ~{{ .SyncProxyName }}() override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  zx_status_t {{ template "SyncRequestMethodSignature" . }} override;
    {{- end }}
  {{- end }}

  private:
  ::fidl::internal::SynchronousProxy proxy_;
  friend class ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
};
#endif // __Fuchsia__
{{- end }}

{{- define "InterfaceDefinition" }}
#ifdef __Fuchsia__
namespace {

{{- range .Methods }}
{{ if .HasRequest }}
extern "C" const fidl_type_t {{ .V1RequestTypeName }};
{{- end }}
{{- if .HasResponse }}
extern "C" const fidl_type_t {{ .V1ResponseTypeName }};
{{- end }}
{{- end }}

}  // namespace

{{ .Name }}::~{{ .Name }}() = default;

{{- if .ServiceName }}
const char {{ .Name }}::Name_[] = {{ .ServiceName }};
{{- end }}

const fidl_type_t* {{ .RequestDecoderName }}::GetType(uint64_t ordinal, bool* out_needs_response) {
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasRequest }}
          {{- range .Ordinals.Reads }}
    case internal::{{ .Name }}:
          {{- end }}
        {{- if .HasResponse }}
      *out_needs_response = true;
        {{- else }}
      *out_needs_response = false;
        {{- end }}
      return &{{ .V1RequestTypeName }};
      {{- end }}
    {{- end }}
    default:
      *out_needs_response = false;
      return nullptr;
  }
}

const fidl_type_t* {{ .ResponseDecoderName }}::GetType(uint64_t ordinal) {
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasResponse }}
          {{- range .Ordinals.Reads }}
    case internal::{{ .Name }}:
          {{- end }}
      return &{{ .V1ResponseTypeName }};
      {{- end }}
    {{- end }}
    default:
      return nullptr;
  }
}

{{ .EventSenderName }}::~{{ .EventSenderName }}() = default;

{{ .SyncName }}::~{{ .SyncName }}() = default;

{{ .ProxyName }}::{{ .ProxyName }}(::fidl::internal::ProxyController* controller)
    : controller_(controller) {
  (void)controller_;
}

{{ .ProxyName }}::~{{ .ProxyName }}() = default;

zx_status_t {{ .ProxyName }}::Dispatch_(::fidl::Message message) {
  zx_status_t status = ZX_OK;
  switch (message.ordinal()) {
    {{- range .Methods }}
      {{- if not .HasRequest }}
        {{- if .HasResponse }}
          {{- range .Ordinals.Reads }}
    case internal::{{ .Name }}:
          {{- end }}
    {
      if (!{{ .Name }}) {
        status = ZX_OK;
        break;
      }
      const char* error_msg = nullptr;
      status = message.Decode(&{{ .V1ResponseTypeName }}, &error_msg);
      if (status != ZX_OK) {
        FIDL_REPORT_DECODING_ERROR(message, &{{ .V1ResponseTypeName }}, error_msg);
        break;
      }
        {{- if .Response }}
      ::fidl::Decoder decoder(std::move(message));
          {{- range $index, $param := .Response }}
      auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .OffsetV1 }});
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

class {{ .ResponseHandlerType }} final : public ::fidl::internal::MessageHandler {
 public:
  {{ .ResponseHandlerType }}({{ $.Name }}::{{ .CallbackType }} callback)
      : callback_(std::move(callback)) {
    ZX_DEBUG_ASSERT_MSG(callback_,
                        "Callback must not be empty for {{ $.Name }}::{{ .Name }}\n");
  }

  zx_status_t OnMessage(::fidl::Message message) override {
    const char* error_msg = nullptr;
    zx_status_t status = message.Decode(&{{ .V1ResponseTypeName }}, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_DECODING_ERROR(message, &{{ .V1ResponseTypeName }}, error_msg);
      return status;
    }
      {{- if .Response }}
    ::fidl::Decoder decoder(std::move(message));
        {{- range $index, $param := .Response }}
    auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .OffsetV1 }});
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
  ::fidl::Encoder _encoder(internal::{{ .Ordinals.Write.Name }});
  controller_->Send(&{{ .V1RequestTypeName }}, {{ $.RequestEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Request -}}
    , &{{ $param.Name }}
  {{- end -}}
  )
  {{- if .HasResponse -}}
    , std::make_unique<{{ .ResponseHandlerType }}>(std::move(callback))
  {{- else -}}
    , nullptr
  {{- end -}}
  );
}
  {{- end }}
{{- end }}

{{ .StubName }}::{{ .StubName }}({{ .ClassName }}* impl) : impl_(impl) {
  (void)impl_;
}

{{ .StubName }}::~{{ .StubName }}() = default;

namespace {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}

class {{ .ResponderType }} final {
 public:
  {{ .ResponderType }}(::fidl::internal::PendingResponse response)
      : response_(std::move(response)) {}

  void operator()({{ template "Params" .Response }}) {
    ::fidl::Encoder _encoder(internal::{{ .Ordinals.Write.Name }});
    response_.Send(&{{ .V1ResponseTypeName }}, {{ $.ResponseEncoderName }}::{{ .Name }}(&_encoder
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

zx_status_t {{ .StubName }}::Dispatch_(
    ::fidl::Message message,
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
        {{- range .Ordinals.Reads }}
    case internal::{{ .Name }}:
        {{- end }}
    {
        {{- if .Request }}
      ::fidl::Decoder decoder(std::move(message));
          {{- range $index, $param := .Request }}
      auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .OffsetV1 }});
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
  ::fidl::Encoder _encoder(internal::{{ .Ordinals.Write.Name }});
  sender_()->Send(&{{ .V1ResponseTypeName }}, {{ $.ResponseEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Response -}}
    , &{{ $param.Name }}
  {{- end -}}
  ));
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
  ::fidl::Encoder _encoder(internal::{{ .Ordinals.Write.Name }});
    {{- if .HasResponse }}
  ::fidl::MessageBuffer buffer_;
  ::fidl::Message response_ = buffer_.CreateEmptyMessage();
  zx_status_t status_ = proxy_.Call(&{{ .V1RequestTypeName }}, &{{ .V1ResponseTypeName }}, {{ $.RequestEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Request -}}
    , &{{ $param.Name }}
  {{- end -}}
  ), &response_);
  if (status_ != ZX_OK)
    return status_;
      {{- if .Response }}
  ::fidl::Decoder decoder_(std::move(response_));
        {{- range $index, $param := .Response }}
  *out_{{ .Name }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder_, {{ .OffsetV1 }});
        {{- end }}
      {{- end }}
  return ZX_OK;
    {{- else }}
  return proxy_.Send(&{{ .V1RequestTypeName }}, {{ $.RequestEncoderName }}::{{ .Name }}(&_encoder
  {{- range $index, $param := .Request -}}
    , &{{ $param.Name }}
  {{- end -}}
  ));
    {{- end }}
}
  {{- end }}
{{- end }}

#endif // __Fuchsia__
{{ end }}

{{- define "InterfaceTraits" }}
{{- end }}

{{- define "InterfaceTestBase" }}
class {{ .Name }}_TestBase : public {{ .Name }} {
  public:
  virtual ~{{ .Name }}_TestBase() { }
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
