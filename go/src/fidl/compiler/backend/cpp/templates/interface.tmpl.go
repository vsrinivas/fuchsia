// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{if $index}}, {{end}}{{$param.Type}} {{$param.Name}}
  {{- end -}}
{{ end }}

{{- define "ParamTypes" -}}
  {{- range $index, $param := . -}}
    {{if $index}}, {{end}}{{$param.Type}}
  {{- end -}}
{{ end }}

{{- define "MethodSignature" -}}
  {{- if .HasResponse -}}
{{ .Name }}({{ template "Params" .Request }}, {{ .Name }}Callback callback)
  {{- else -}}
{{ .Name }}({{ template "Params" .Request }})
  {{- end -}}
{{ end -}}

{{- define "InterfaceDeclaration" -}}
class {{ .Name }}Proxy;
class {{ .Name }}Stub;

class {{ .Name }} {
 public:
  using Proxy_ = {{ .Name }}Proxy;
  using Stub_ = {{ .Name }}Stub;
  virtual ~{{ .Name }}();

  {{ range $method := .Methods -}}
    {{- if $method.HasResponse -}}
  using {{ $method.Name }}Callback =
      std::function<void({{ template "Params" .Response }})>;
    {{- end }}
  virtual void {{ template "MethodSignature" . }} = 0;
  {{- end }}
};

using {{ .Name }}Ptr = ::fidl::InterfacePtr<{{ .Name }}>;

class {{ .Name }}Proxy : public {{ .Name }} {
 public:
  explicit {{ .Name }}Proxy(::fidl::internal::ProxyController* controller);
  ~{{ .Name }}Proxy() override;

  {{- range $method := .Methods }}
  void {{ template "MethodSignature" . }} override;
  {{- end }}

 private:
  {{ .Name }}Proxy(const {{ .Name }}Proxy&) = delete;
  {{ .Name }}Proxy& operator=(const {{ .Name }}Proxy&) = delete;

  ::fidl::internal::ProxyController* controller_;
};

class {{ .Name }}Stub : public ::fidl::internal::Stub {
 public:
  explicit {{ .Name }}Stub({{ .Name }}* impl);
  ~{{ .Name }}Stub() override;

  zx_status_t Dispatch(::fidl::Message message,
                       ::fidl::internal::PendingResponse response) override;

 private:
  {{ .Name }}* impl_;
};
{{end}}

{{- define "InterfaceDefinition" -}}
namespace {
{{ range $method := .Methods }}
constexpr uint32_t k{{ .Name }}_{{ $method.Name }}_Ordinal = {{ $method.Ordinal }}u;
{{- end }}

}  // namespace

{{ .Name }}::~{{ .Name }}() = default;

{{ .Name }}Proxy::{{ .Name }}Proxy(::fidl::internal::ProxyController* controller)
    : controller_(controller) {}

{{ .Name }}Proxy::~{{ .Name }}Proxy() = default;

{{ range $method := .Methods }}
void {{ .Name }}Proxy::{{ template "MethodSignature" . }} {
  // TODO(abarth): Implement method.
}
{{- end }}

{{ .Name }}Stub::{{ .Name }}Stub({{ .Name }}* impl) : impl_(impl) {}

{{ .Name }}Stub::~{{ .Name }}Stub() = default;

zx_status_t {{ .Name }}Stub::Dispatch(
    ::fidl::Message message,
    ::fidl::internal::PendingResponse response) {
  zx_status_t status = ZX_OK;
  switch (message.ordinal()) {
    {{- range $method := .Methods }}
    case k{{ .Name }}_{{ $method.Name }}_Ordinal: {
      // TODO(abarth): Dispatch method.
      break;
    }
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
