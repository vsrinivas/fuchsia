// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceForwardDeclaration" -}}
class {{ .Name }};
using {{ .Name }}Ptr = ::fidl::InterfacePtr<{{ .Name }}>;
class {{ .ProxyName }};
class {{ .StubName }};
{{- end }}

{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Name|$param.Type.Decorate }}
  {{- end -}}
{{ end }}

{{- define "ParamTypes" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ ""|$param.Type.Decorate }}
  {{- end -}}
{{ end }}

{{- define "RequestMethodSignature" -}}
  {{- if .HasResponse -}}
{{ .Name }}({{ template "Params" .Request }}{{ if .Request }}, {{ end }}{{ .CallbackType }} callback)
  {{- else -}}
{{ .Name }}({{ template "Params" .Request }})
  {{- end -}}
{{ end -}}

{{- define "InterfaceDeclaration" -}}
class {{ .Name }} {
 public:
  using Proxy_ = {{ .ProxyName }};
  using Stub_ = {{ .StubName }};
  virtual ~{{ .Name }}();

  {{- range .Methods }}
    {{- if .HasRequest }}
      {{- if .HasResponse }}
  using {{ .CallbackType }} =
      std::function<void({{ template "Params" .Response }})>;
      {{- end }}
  virtual void {{ template "RequestMethodSignature" . }} = 0;
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
{{end}}

{{- define "InterfaceDefinition" -}}
namespace {
{{ range .Methods }}
  {{- if .HasRequest }}
constexpr uint32_t {{ .OrdinalName }} = {{ .Ordinal }}u;
  {{- end }}
{{- end }}

}  // namespace

{{ .Name }}::~{{ .Name }}() = default;

{{ .ProxyName }}::{{ .ProxyName }}(::fidl::internal::ProxyController* controller)
    : controller_(controller) {}

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
    zx_status_t status = message.Decode(nullptr, &error_msg);
    if (status != ZX_OK)
      return status;
    // TODO(TO-490): Actually unpack the arguments from the message.
    callback_(
      {{- range $index, $param := .Response -}}
        {{- if $index }}, {{ end }}{{ $param.Type.Decl }}()
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
  ::fidl::MessageBuilder builder(nullptr);
  builder.header()->ordinal = {{ .OrdinalName }};
    {{- range .Request }}
  ::fidl::PutAt(&builder, builder.New<::fidl::ViewOf<decltype({{ .Name }})>::type>(), &{{ .Name }});
    {{- end -}}
    {{- if .HasResponse }}
  controller_->Send(&builder, std::make_unique<{{ .ResponseHandlerType }}>(std::move(callback)));
    {{- else }}
  controller_->Send(&builder, nullptr);
    {{- end }}
}
  {{- end }}
{{- end }}

{{ .StubName }}::{{ .StubName }}({{ .Name }}* impl) : impl_(impl) {}

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
    ::fidl::MessageBuilder builder(nullptr);
    builder.header()->ordinal = {{ .OrdinalName }};
      {{- range .Response }}
    ::fidl::PutAt(&builder, builder.New<::fidl::ViewOf<decltype({{ .Name }})>::type>(), &{{ .Name }});
      {{- end }}
    response_.Send(&builder);
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
      status = message.Decode(nullptr, &error_msg);
      if (status != ZX_OK)
        break;
      // TODO(TO-490): Actually unpack the arguments from the message.
      impl_->{{ .Name }}(
        {{- range $index, $param := .Request -}}
          {{- if $index }}, {{ end }}{{ $param.Type.Decl }}()
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
{{- end }}
`
