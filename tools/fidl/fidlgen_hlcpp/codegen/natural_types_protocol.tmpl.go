// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const protocolTemplateNaturalTypes = `
{{- define "ProtocolForwardDeclaration/NaturalTypes" }}
{{ EnsureNamespace . }}
{{- IfdefFuchsia -}}
{{- .Docs }}
class {{ .Name }};
using {{ .Name }}Handle = ::fidl::InterfaceHandle<{{ .Name }}>;
{{- EndifFuchsia -}}
{{- end }}

{{- define "PointerParams" -}}
  {{- range $index, $param := . -}}
    , {{ $param.Type }}* {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "ProtocolDeclaration/NaturalTypes" }}
{{ EnsureNamespace . }}
{{- IfdefFuchsia -}}

{{- range .Methods }}
  {{- if .HasRequest }}
  {{ EnsureNamespace .Request.HlCodingTable }}
  __LOCAL extern "C" const fidl_type_t {{ .Request.HlCodingTable.Name }};
  {{- end }}
{{- end }}

{{ EnsureNamespace .RequestEncoder }}
class {{ .RequestEncoder.Name }} {
 public:
  {{- with $protocol := . }}
  {{- range .Methods }}
  {{- if .HasRequest }}
  static ::fidl::HLCPPOutgoingMessage {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .RequestArgs }}) {
    fidl_trace(WillHLCPPEncode);
    switch (_encoder->wire_format()) {
      case ::fidl::internal::WireFormatVersion::kV1:
        _encoder->Alloc({{ .Request.TypeShapeV1.InlineSize }} - sizeof(fidl_message_header_t));
        break;
      case ::fidl::internal::WireFormatVersion::kV2:
        _encoder->Alloc({{ .Request.TypeShapeV1.InlineSize }} - sizeof(fidl_message_header_t));
        break;
    }

    if (_encoder->wire_format() == ::fidl::internal::WireFormatVersion::kV1) {
      {{- range .RequestArgs }}
      {{- if .HandleInformation }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV1 }}, ::fidl::HandleInformation {
        .object_type = {{ .HandleInformation.ObjectType }},
        .rights = {{ .HandleInformation.Rights }},
      });
      {{ else }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV1 }});
      {{ end -}}
      {{- end }}
    } else {
      {{- range .RequestArgs }}
      {{- if .HandleInformation }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV2 }}, ::fidl::HandleInformation {
        .object_type = {{ .HandleInformation.ObjectType }},
        .rights = {{ .HandleInformation.Rights }},
      });
      {{ else }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV2 }});
      {{ end -}}
      {{- end }}
    }

    fidl_trace(DidHLCPPEncode, &{{ .Request.HlCodingTable }}, _encoder->GetPtr<const char>(0), _encoder->CurrentLength(), _encoder->CurrentHandleCount());

    return _encoder->GetMessage();
  }
  {{- end }}
  {{- end }}
  {{- end }}
};

{{- range .Methods }}
  {{- if .HasResponse }}
  {{ EnsureNamespace .Response.HlCodingTable }}
  __LOCAL extern "C" const fidl_type_t {{ .Response.HlCodingTable.Name }};
  {{- end }}
{{- end }}

{{ EnsureNamespace .ResponseEncoder }}
class {{ .ResponseEncoder.Name }} {
 public:
  {{- with $protocol := . }}
  {{- range .Methods }}
  {{- if .HasResponse }}
  static ::fidl::HLCPPOutgoingMessage {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .ResponseArgs }}) {
    fidl_trace(WillHLCPPEncode);
    switch (_encoder->wire_format()) {
      case ::fidl::internal::WireFormatVersion::kV1:
        _encoder->Alloc({{ .Response.TypeShapeV1.InlineSize }} - sizeof(fidl_message_header_t));
        break;
      case ::fidl::internal::WireFormatVersion::kV2:
        _encoder->Alloc({{ .Response.TypeShapeV2.InlineSize }} - sizeof(fidl_message_header_t));
        break;
    }


    if (_encoder->wire_format() == ::fidl::internal::WireFormatVersion::kV1) {
      {{- range .ResponseArgs }}
      {{- if .HandleInformation }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV1 }}, ::fidl::HandleInformation {
        .object_type = {{ .HandleInformation.ObjectType }},
        .rights = {{ .HandleInformation.Rights }},
      });
      {{ else }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV1 }});
      {{ end -}}
      {{- end }}
    } else {
      {{- range .ResponseArgs }}
      {{- if .HandleInformation }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV2 }}, ::fidl::HandleInformation {
        .object_type = {{ .HandleInformation.ObjectType }},
        .rights = {{ .HandleInformation.Rights }},
      });
      {{ else }}
      ::fidl::Encode(_encoder, {{ .Name }}, {{ .OffsetV2 }});
      {{ end -}}
      {{- end }}
    }

    fidl_trace(DidHLCPPEncode, &{{ .Response.HlCodingTable }}, _encoder->GetPtr<const char>(0), _encoder->CurrentLength(), _encoder->CurrentHandleCount());
    return _encoder->GetMessage();
  }
  {{- end }}
  {{- end }}
  {{- end }}
};

{{- EndifFuchsia -}}
{{- end }}
`
