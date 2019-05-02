// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

// OvernetInternal transports are used by Overnet to communicate between peers,
// and are not intended for general purpose consumption.

const OvernetInternal = `
{{- define "Header" }}
#pragma once

#include "src/connectivity/overnet/lib/protocol/fidl_stream.h"

#include "{{ range $index, $path := .Library }}{{ if $index }}/{{ end }}{{ $path }}{{ end }}/cpp/fidl.h"
  {{- range .Library }}
  namespace {{ . }} {
  {{- end }}
  {{ range .Decls }}
    {{- if Eq .Kind Kinds.Interface }}
      {{ if index .Transports "OvernetInternal" }}
        {{ template "InterfaceForwardDeclaration" . }}
      {{ end }}
    {{- end }}
  {{ end }}
  {{ range .Decls }}
    {{- if Eq .Kind Kinds.Interface }}
      {{ if index .Transports "OvernetInternal" }}
        {{ template "InterfaceDeclaration" . }}
      {{ end }}
    {{- end }}
  {{ end }}
  {{ range .Decls }}
    {{- if Eq .Kind Kinds.Interface }}
      {{ if index .Transports "OvernetInternal" }}
        {{ template "InterfaceTraits" . }}
      {{ end }}
    {{- end }}
  {{ end }}
  {{- range .LibraryReversed }}
  }  // namespace {{ . }}
  {{- end }}
{{ end }}

{{- define "Source" }}
#include <{{ .PrimaryHeader }}>
#include "lib/fidl/cpp/internal/implementation.h"
  {{- range .Library }}
  namespace {{ . }} {
  {{- end }}
  {{ range .Decls }}
    {{- if Eq .Kind Kinds.Interface }}
      {{ if index .Transports "OvernetInternal" }}
        {{ template "InterfaceDefinition" . }}
      {{ end }}
    {{- end }}
  {{ end }}
  {{- range .LibraryReversed }}
  }  // namespace {{ . }}
  {{- end }}
{{ end }}

{{- define "InterfaceForwardDeclaration" }}
class {{ .Name }};
class {{ .ProxyName }};
class {{ .StubName }};
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

{{- define "InterfaceDeclaration" }}
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Name }} {
 public:
  using Proxy_ = {{ .ProxyName }};
  using Stub_ = {{ .StubName }};
  {{- if .ServiceName }}
  static const char Name_[];
  {{- end }}
  virtual ~{{ .Name }}();

  {{- range .Methods }}
    {{- if .HasResponse }}
      {{- if .HasRequest }}
  using {{ .CallbackType }} =
      {{ .CallbackWrapper }}<void({{ template "ParamTypes" .Response }})>;
      {{- end }}
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
    {{- else if .HasResponse }}
      {{- if .Transitional }}
  virtual void {{ template "EventMethodSignature" . }} { };
      {{- else }}
  virtual void {{ template "EventMethodSignature" . }} = 0;
      {{- end }}
    {{- end }}
  {{- end }}
};

class {{ .ProxyName }} : public ::overnet::FidlStream, public {{ .Name }} {
 public:
 {{ .ProxyName }}() = default;
  ~{{ .ProxyName }}() override;

  {{- range .Methods }}
    {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }} override final;
    {{- end }}
  {{- end }}

 private:
  zx_status_t Dispatch_(::fidl::Message* message) override final;
  {{ .ProxyName }}(const {{ .ProxyName }}&) = delete;
  {{ .ProxyName }}& operator=(const {{ .ProxyName }}&) = delete;
};

class {{ .StubName }} : public ::overnet::FidlStream, public {{ .Name }} {
 public:
  typedef class {{ .Name }} {{ .ClassName }};
  {{ .StubName }}() = default;
  ~{{ .StubName }}() override;

  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- if .HasResponse }}
  virtual void {{ template "EventMethodSignature" . }} override final;
      {{- end }}
    {{- end }}
  {{- end }}

 private:
  zx_status_t Dispatch_(::fidl::Message* message) override final;
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

{{ .ProxyName }}::~{{ .ProxyName }}() = default;

zx_status_t {{ .ProxyName }}::Dispatch_(::fidl::Message* message) {
  zx_status_t status = ZX_OK;
  switch (message->ordinal()) {
    {{- range .Methods }}
      {{- if not .HasRequest }}
        {{- if .HasResponse }}
    case {{ .OrdinalName }}: {
      const char* error_msg = nullptr;
      status = message->Decode(&{{ .ResponseTypeName }}, &error_msg);
      if (status != ZX_OK) {
        FIDL_REPORT_DECODING_ERROR(*message, &{{ .ResponseTypeName }}, error_msg);
        ZX_ASSERT(status != ZX_ERR_NOT_SUPPORTED);
        break;
      }
        {{- if .Response }}
      ::fidl::Decoder decoder(std::move(*message));
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
void {{ $.ProxyName }}::{{ template "RequestMethodSignature" . }} {
  ::fidl::Encoder _encoder({{ .OrdinalName }});
    {{- if .Request }}
  _encoder.Alloc({{ .RequestSize }} - sizeof(fidl_message_header_t));
      {{- range .Request }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
      {{- end }}
    {{- end }}
    {{- if .HasResponse }}
  Send_(_encoder.GetMessage(), [callback=std::move(callback)](::fidl::Message message) {
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
    callback(
      {{- range $index, $param := .Response -}}
        {{- if $index }}, {{ end }}std::move(arg{{ $index }})
      {{- end -}}
    );
    return ZX_OK;
  });
    {{- else }}
  Send_(_encoder.GetMessage());
    {{- end }}
}
  {{- end }}
{{- end }}

{{ .StubName }}::~{{ .StubName }}() = default;

zx_status_t {{ .StubName }}::Dispatch_(::fidl::Message* message) {
  zx_status_t status = ZX_OK;
  switch (message->ordinal()) {
    {{- range .Methods }}
      {{- if .HasRequest }}
    case {{ .OrdinalName }}: {
      const char* error_msg = nullptr;
      status = message->Decode(&{{ .RequestTypeName }}, &error_msg);
      if (status != ZX_OK) {
        FIDL_REPORT_DECODING_ERROR(*message, &{{ .RequestTypeName }}, error_msg);
        ZX_ASSERT(status != ZX_ERR_NOT_SUPPORTED);
        break;
      }
        {{- if .Request }}
      ::fidl::Decoder decoder(std::move(*message));
          {{- range $index, $param := .Request }}
      auto arg{{ $index }} = ::fidl::DecodeAs<{{ .Type.Decl }}>(&decoder, {{ .Offset }});
          {{- end }}
        {{- end }}
      {{ .Name }}(
        {{- range $index, $param := .Request -}}
          {{- if $index }}, {{ end }}std::move(arg{{ $index }})
        {{- end -}}
        {{- if .HasResponse -}}
          {{- if .Request }}, {{ end -}}[this]({{ template "Params" .Response }}) {
            ::fidl::Encoder _encoder({{ .OrdinalName }});
            {{- if .Response }}
            _encoder.Alloc({{ .ResponseSize }} - sizeof(fidl_message_header_t));
              {{- range .Response }}
            ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
              {{- end }}
            {{- end }}
            Send_(_encoder.GetMessage());
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
  ::fidl::Encoder _encoder({{ .OrdinalName }});
    {{- if .Response }}
  _encoder.Alloc({{ .ResponseSize }} - sizeof(fidl_message_header_t));
      {{- range .Response }}
  ::fidl::Encode(&_encoder, &{{ .Name }}, {{ .Offset }});
      {{- end }}
    {{- end }}
  Send_(_encoder.GetMessage());
}
    {{- end }}
  {{- end }}
{{- end }}

{{ end }}

{{- define "OvernetInternalTestBase" }}
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

{{- define "InterfaceTraits" }}
{{- end }}
`
