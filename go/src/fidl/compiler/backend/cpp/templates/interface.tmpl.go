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
class {{ .SyncName }};
using {{ .Name }}SyncPtr = ::fidl::SynchronousInterfacePtr<{{ .Name }}>;
class {{ .SyncProxyName }};
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
  using Sync_ = {{ .SyncName }};
  {{- if .ServiceName }}
  static const char Name_[];
  {{- end }}
  virtual ~{{ .Name }}();

  {{- range .Methods }}
    {{- if .HasRequest }}
      {{- if .HasResponse }}
  using {{ .CallbackType }} =
      std::function<void({{ template "ParamTypes" .Response }})>;
      {{- end }}
  virtual void {{ template "RequestMethodSignature" . }} = 0;
    {{- end }}
  {{- end }}
};

class {{ .SyncName }} {
 public:
  using Proxy_ = {{ .SyncProxyName }};
  virtual ~{{ .SyncName }}();

  {{- range .Methods }}
    {{- if .HasRequest }}
  virtual bool {{ template "SyncRequestMethodSignature" . }} = 0;
    {{- end }}
  {{- end }}
};

class {{ .ProxyName }} : public {{ .Name }} {
 public:
  explicit {{ .ProxyName }}(::fidl::internal::ProxyController* controller);
  ~{{ .ProxyName }}() override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }} override;
    {{- end }}
  {{- end }}

 private:
  {{ .ProxyName }}(const {{ .ProxyName }}&) = delete;
  {{ .ProxyName }}& operator=(const {{ .ProxyName }}&) = delete;

  ::fidl::internal::ProxyController* controller_;
};

class {{ .StubName }} : public ::fidl::internal::Stub {
 public:
  explicit {{ .StubName }}({{ .Name }}* impl);
  ~{{ .StubName }}() override;

  zx_status_t Dispatch(::fidl::Message message,
                       ::fidl::internal::PendingResponse response) override;

 private:
  {{ .Name }}* impl_;
};

class {{ .SyncProxyName }} : public {{ .SyncName }} {
 public:
  explicit {{ .SyncProxyName }}(::zx::channel channel);
  ~{{ .SyncProxyName }}() override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  bool {{ template "SyncRequestMethodSignature" . }} override;
    {{- end }}
  {{- end }}

  private:
  ::fidl::internal::SynchronousProxy proxy_;
};

{{- end }}

{{- define "InterfaceDefinition" }}
namespace {

{{ range .Methods }}
  {{- if .HasRequest }}
constexpr uint32_t {{ .OrdinalName }} = {{ .Ordinal }}u;
extern "C" const fidl_type_t {{ .RequestTypeName }};
    {{- if .HasResponse }}
extern "C" const fidl_type_t {{ .ResponseTypeName }};
    {{- end }}
  {{- end }}
{{- end }}

}  // namespace

{{ .Name }}::~{{ .Name }}() = default;

{{- if .ServiceName }}
const char {{ .Name }}::Name_[] = {{ .ServiceName }};
{{- end }}

{{ .SyncName }}::~{{ .SyncName }}() = default;

{{ .ProxyName }}::{{ .ProxyName }}(::fidl::internal::ProxyController* controller)
    : controller_(controller) {
  (void) controller_;
}

{{ .ProxyName }}::~{{ .ProxyName }}() = default;

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
      fprintf(stderr, "error: fidl_decode: %s\n", error_msg);
      return status;
    }
      {{- if .Response }}
    ::fidl::Decoder decoder(std::move(message));
      {{- end }}
    callback_(
      {{- range $index, $param := .Response -}}
        {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .Offset }})
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

zx_status_t {{ .StubName }}::Dispatch(
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
        fprintf(stderr, "error: fidl_decode: %s\n", error_msg);
        break;
      }
        {{- if .Request }}
      ::fidl::Decoder decoder(std::move(message));
        {{- end }}
      impl_->{{ .Name }}(
        {{- range $index, $param := .Request -}}
          {{- if $index }}, {{ end }}::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .Offset }})
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

{{ .SyncProxyName }}::{{ .SyncProxyName }}(::zx::channel channel)
  : proxy_(::std::move(channel)) {}

{{ .SyncProxyName }}::~{{ .SyncProxyName }}() = default;

{{- range .Methods }}
  {{- if .HasRequest }}
bool {{ $.SyncProxyName }}::{{ template "SyncRequestMethodSignature" . }} {
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
    return false;
      {{- if .Response }}
  ::fidl::Decoder decoder_(std::move(response_));
        {{- range $index, $param := .Response }}
  *out_{{ .Name }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder_, {{ .Offset }});
        {{- end }}
      {{- end }}
  return true;
    {{- else }}
  return proxy_.Send(&{{ .RequestTypeName }}, _encoder.GetMessage()) == ZX_OK;
    {{- end }}
}
  {{- end }}
{{- end }}

{{ end }}
`
