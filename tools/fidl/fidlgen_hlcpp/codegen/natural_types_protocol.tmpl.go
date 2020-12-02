// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const protocolTemplateNaturalTypes = `
{{- define "ProtocolForwardDeclaration/NaturalTypes" }}
#ifdef __Fuchsia__
{{- range .DocComments }}
///{{ . }}
{{- end }}
class {{ .Name }};
using {{ .Name }}Handle = ::fidl::InterfaceHandle<{{ .Name }}>;
#endif  // __Fuchsia__
{{- end }}

{{- define "PointerParams" -}}
  {{- range $index, $param := . -}}
    , {{ $param.Type.FullDecl }}* {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "ProtocolDeclaration/NaturalTypes" }}
#ifdef __Fuchsia__

namespace _internal {
  {{- range .Methods }}
  {{- if .HasRequest }}
  extern "C" const fidl_type_t {{ .RequestTypeName }};
  {{- end }}
  {{- end }}
}

class {{ .RequestEncoderName }} {
 public:
  {{- with $protocol := . }}
  {{- range .Methods }}
  {{- if .HasRequest }}
  static ::fidl::HLCPPOutgoingMessage {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .Request }}) {
    fidl_trace(WillHLCPPEncode);
    _encoder->Alloc({{ .RequestSize }} - sizeof(fidl_message_header_t));

    {{- range .Request }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .Offset }});
    {{- end }}

    fidl_trace(DidHLCPPEncode, &_internal::{{ .RequestTypeName }}, _encoder->GetPtr<const char>(0), _encoder->CurrentLength(), _encoder->CurrentHandleCount());

    return _encoder->GetMessage();
  }
  {{- end }}
  {{- end }}
  {{- end }}
};

namespace _internal {
  {{- range .Methods }}
  {{- if .HasResponse }}
  extern "C" const fidl_type_t {{ .ResponseTypeName }};
  {{- end }}
  {{- end }}
}

class {{ .ResponseEncoderName }} {
 public:
  {{- with $protocol := . }}
  {{- range .Methods }}
  {{- if .HasResponse }}
  static ::fidl::HLCPPOutgoingMessage {{ .Name }}(::fidl::Encoder* _encoder{{ template "PointerParams" .Response }}) {
    fidl_trace(WillHLCPPEncode);
    _encoder->Alloc({{ .ResponseSize }} - sizeof(fidl_message_header_t));

    {{- range .Response }}
    ::fidl::Encode(_encoder, {{ .Name }}, {{ .Offset }});
    {{- end }}

    fidl_trace(DidHLCPPEncode, &_internal::{{ .ResponseTypeName }}, _encoder->GetPtr<const char>(0), _encoder->CurrentLength(), _encoder->CurrentHandleCount());
    return _encoder->GetMessage();
  }
  {{- end }}
  {{- end }}
  {{- end }}
};

#endif  // __Fuchsia__
{{- end }}
`
