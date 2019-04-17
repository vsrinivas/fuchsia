// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceForwardDeclaration" }}
class {{ .Name }};
using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
class {{ .ProxyName }};
class {{ .StubName }};
class {{ .EventSenderName }};
{{- end }}

{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.OvernetEmbeddedDecl }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "OutParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.OvernetEmbeddedDecl }}* out_{{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "ParamTypes" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.OvernetEmbeddedDecl }}
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

{{- define "InterfaceDeclaration" }}
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Name }} {
 public:
  using Proxy_ = {{ .ProxyName }};
  using Stub_ = {{ .StubName }};
  using EventSender_ = {{ .EventSenderName }};
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
  virtual void {{ template "RequestMethodSignature" . }} { };
      {{- else }}
  virtual void {{ template "RequestMethodSignature" . }} = 0;
      {{- end }}
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

class {{ .ProxyName }} : public ::overnet::internal::FidlChannel, public {{ .Name }} {
 public:
  explicit {{ .ProxyName }}(::overnet::ClosedPtr<::overnet::ZxChannel> channel);
  ~{{ .ProxyName }}() override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }} override;
    {{- else if .HasResponse }}
  {{ .CallbackType }} {{ .Name }};
    {{- end }}
  {{- end }}

 private:
  zx_status_t Dispatch_(::fuchsia::overnet::protocol::ZirconChannelMessage message) override;

  {{ .ProxyName }}(const {{ .ProxyName }}&) = delete;
  {{ .ProxyName }}& operator=(const {{ .ProxyName }}&) = delete;
};

class {{ .StubName }} : public ::overnet::internal::FidlChannel, public {{ .EventSenderName }} {
 public:
  typedef class {{ .Name }} {{ .ClassName }};
  explicit {{ .StubName }}({{ .ClassName }}* impl, ::overnet::ClosedPtr<::overnet::ZxChannel> channel);
  ~{{ .StubName }}() override;

  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- if .HasResponse }}
  void {{ template "EventMethodSignature" . }} override;
      {{- end }}
    {{- end }}
  {{- end }}

 private:
  zx_status_t Dispatch_(::fuchsia::overnet::protocol::ZirconChannelMessage message) override;

  {{ .ClassName }}* impl_;
};
{{- end }}

{{- define "InterfaceDefinition" }}
namespace {

{{- range .Methods }}
  {{ if ne .GenOrdinal .Ordinal }}
constexpr uint32_t {{ .GenOrdinalName }} = {{ .GenOrdinal }}u;
  {{- end }}
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

{{ .ProxyName }}::{{ .ProxyName }}(::overnet::ClosedPtr<::overnet::ZxChannel> channel)
    : FidlChannel(::std::move(channel)) {}

{{ .ProxyName }}::~{{ .ProxyName }}() = default;

zx_status_t {{ .ProxyName }}::Dispatch_(::fuchsia::overnet::protocol::ZirconChannelMessage message) {
  zx_status_t status = ZX_OK;
  ::overnet::internal::Decoder decoder(std::move(message), io_.get());
  switch (decoder.ordinal()) {
    {{- range .Methods }}
      {{- if not .HasRequest }}
        {{- if .HasResponse }}
          {{- if ne .GenOrdinal .Ordinal }}
    case {{ .GenOrdinalName }}:
          {{- end }}
    case {{ .OrdinalName }}: {
      if (!{{ .Name }}) {
        status = ZX_OK;
        break;
      }
      const char* error_msg = nullptr;
      status = decoder.FidlDecode(&{{ .ResponseTypeName }}, &error_msg);
      if (status != ZX_OK) {
        break;
      }
        {{- if .Response }}
          {{- range $index, $param := .Response }}
      auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.OvernetEmbeddedDecl }}>(&decoder, {{ .Offset }});
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

}  // namespace
{{- end }}
void {{ $.ProxyName }}::{{ template "RequestMethodSignature" . }} {
  ::overnet::internal::Encoder _encoder({{ .OrdinalName }}, io_.get());
    {{- if .Request }}
  _encoder.Alloc({{ .RequestSize }} - sizeof(fidl_message_header_t));
      {{- range .Request }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
      {{- end }}
    {{- end }}
    {{- if .HasResponse }}
  io_->Send(&{{ .RequestTypeName }}, _encoder.GetMessage(),
       [callback = std::move(callback), io = io_](fuchsia::overnet::protocol::ZirconChannelMessage response) -> zx_status_t {
    ::overnet::internal::Decoder decoder(std::move(response), io.get());
    const char* error_msg = nullptr;
    zx_status_t status = decoder.FidlDecode(&{{ .ResponseTypeName }}, &error_msg);
    if (status != ZX_OK) {
      return status;
    }
      {{- if .Response }}
        {{- range $index, $param := .Response }}
    auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.OvernetEmbeddedDecl }}>(&decoder, {{ .Offset }});
        {{- end }}
      {{- end }}
    callback(
      {{- range $index, $param := .Response -}}
        {{- if $index }}, {{ end }}std::move(arg{{ $index }})
      {{- end -}}
    );
    return ZX_OK;
  });
    {{- else }}
  io_->Send(&{{ .RequestTypeName }}, _encoder.GetMessage());
    {{- end }}
}
  {{- end }}
{{- end }}

{{ .StubName }}::{{ .StubName }}({{ .ClassName }}* impl, ::overnet::ClosedPtr<::overnet::ZxChannel> channel)
    : ::overnet::internal::FidlChannel(std::move(channel)), impl_(impl) {
  (void)impl_;
}

{{ .StubName }}::~{{ .StubName }}() = default;

zx_status_t {{ .StubName }}::Dispatch_(
    ::fuchsia::overnet::protocol::ZirconChannelMessage message) {
  zx_status_t status = ZX_OK;
  ::overnet::internal::Decoder decoder(std::move(message), io_.get());
  uint32_t ordinal = decoder.ordinal();
  switch (ordinal) {
    {{- range .Methods }}
      {{- if .HasRequest }}
        {{- if ne .GenOrdinal .Ordinal }}
    case {{ .GenOrdinalName }}:
        {{- end }}
    case {{ .OrdinalName }}: {
      const char* error_msg = nullptr;
      status = decoder.FidlDecode(&{{ .RequestTypeName }}, &error_msg);
      if (status != ZX_OK) {
        break;
      }
        {{- if .Request }}
          {{- range $index, $param := .Request }}
      auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.OvernetEmbeddedDecl }}>(&decoder, {{ .Offset }});
          {{- end }}
        {{- end }}
      impl_->{{ .Name }}(
        {{- range $index, $param := .Request -}}
          {{- if $index }}, {{ end }}std::move(arg{{ $index }})
        {{- end -}}
        {{- if .HasResponse -}}
          {{- if .Request }}, {{ end -}}
          [this]({{ template "Params" .Response }}) {
            ::overnet::internal::Encoder _encoder({{ .OrdinalName }}, io_.get());
            {{- if .Response }}
            _encoder.Alloc({{ .ResponseSize }} - sizeof(fidl_message_header_t));
              {{- range .Response }}
            ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
              {{- end }}
            {{- end }}
            io_->Send(&{{ .ResponseTypeName }}, _encoder.GetMessage());
          }
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
  ::overnet::internal::Encoder _encoder({{ .OrdinalName }}, io_.get());
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

{{ end }}

{{- define "InterfaceTraits" }}
template <>
struct CodingTraits<std::unique_ptr<{{ .Namespace }}::embedded::{{ .ProxyName }}>> {
  static void Encode(overnet::internal::Encoder* encoder,
    std::unique_ptr<{{ .Namespace }}::embedded::{{ .ProxyName }}>* interface,
    size_t offset) {
    auto channel = (*interface)->TakeChannel_();
    interface->reset();
    ::fidl::Encode(encoder, &channel, offset);
  }
  static void Decode(overnet::internal::Decoder* decoder,
    std::unique_ptr<{{ .Namespace }}::embedded::{{ .ProxyName }}>* interface,
    size_t offset) {
    *interface = ::std::make_unique<{{ .Namespace }}::embedded::{{ .ProxyName }}>(
      ::overnet::ZxChannel::Decode(decoder, offset));
  }
};

template <>
struct CodingTraits<std::unique_ptr<{{ .Namespace }}::embedded::{{ .StubName }}>> {
  static void Encode(overnet::internal::Encoder* encoder,
    std::unique_ptr<{{ .Namespace }}::embedded::{{ .StubName }}>* interface,
    size_t offset) {
    auto channel = (*interface)->TakeChannel_();
    interface->reset();
    ::fidl::Encode(encoder, &channel, offset);
  }
  static void Decode(overnet::internal::Decoder* decoder,
    std::unique_ptr<{{ .Namespace }}::embedded::{{ .StubName }}>* interface,
    size_t offset);
};
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
