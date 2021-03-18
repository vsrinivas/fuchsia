// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const protocolTemplateNaturalTypes = `
{{- define "ProtocolForwardDeclaration/NaturalTypes" }}
{{ EnsureNamespace . }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- range .DocComments }}
///{{ . }}
{{- end }}
class {{ .Name }};
using {{ .Name }}Handle = ::fidl::InterfaceHandle<{{ .Name }}>;
{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}

{{- define "PointerParams" -}}
  {{- range $index, $param := . -}}
    , {{ $param.Type }}* {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "ProtocolDeclaration/NaturalTypes" }}
{{ EnsureNamespace . }}
#ifdef __Fuchsia__
{{- PushNamespace }}

{{- range .Methods }}
  {{- if .HasRequest }}
  {{ EnsureNamespace .Request.CodingTable }}
  extern "C" const fidl_type_t {{ .Request.CodingTable.Name }};
  {{- end }}
{{- end }}

{{ EnsureNamespace .RequestEncoderName }}
class {{ .RequestEncoderName.Name }} {
 public:
  {{- with $protocol := . }}
  {{- range .Methods }}
  {{- if .HasRequest }}
  static ::fidl::HLCPPOutgoingMessage {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .RequestArgs }}) {
    fidl_trace(WillHLCPPEncode);
    _encoder->Alloc({{ .Request.InlineSize }} - sizeof(fidl_message_header_t));

    {{- range .RequestArgs }}
    {{- if .HandleInformation }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .Offset }}, ::fidl::HandleInformation {
      .object_type = {{ .HandleInformation.ObjectType }},
      .rights = {{ .HandleInformation.Rights }},
    });
    {{ else }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .Offset }});
    {{ end -}}
    {{- end }}

    fidl_trace(DidHLCPPEncode, &{{ .Request.CodingTable }}, _encoder->GetPtr<const char>(0), _encoder->CurrentLength(), _encoder->CurrentHandleCount());

    return _encoder->GetMessage();
  }
  {{- end }}
  {{- end }}
  {{- end }}
};

{{- range .Methods }}
  {{- if .HasResponse }}
  {{ EnsureNamespace .Response.CodingTable }}
  extern "C" const fidl_type_t {{ .Response.CodingTable.Name }};
  {{- end }}
{{- end }}

{{ EnsureNamespace .ResponseEncoderName }}
class {{ .ResponseEncoderName.Name }} {
 public:
  {{- with $protocol := . }}
  {{- range .Methods }}
  {{- if .HasResponse }}
  static ::fidl::HLCPPOutgoingMessage {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .ResponseArgs }}) {
    fidl_trace(WillHLCPPEncode);
    _encoder->Alloc({{ .Response.InlineSize }} - sizeof(fidl_message_header_t));

    {{- range .ResponseArgs }}
    {{- if .HandleInformation }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .Offset }}, ::fidl::HandleInformation {
      .object_type = {{ .HandleInformation.ObjectType }},
      .rights = {{ .HandleInformation.Rights }},
    });
    {{ else }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .Offset }});
    {{ end -}}
    {{- end }}

    fidl_trace(DidHLCPPEncode, &{{ .Response.CodingTable }}, _encoder->GetPtr<const char>(0), _encoder->CurrentLength(), _encoder->CurrentHandleCount());
    return _encoder->GetMessage();
  }
  {{- end }}
  {{- end }}
  {{- end }}
};

{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}
`
