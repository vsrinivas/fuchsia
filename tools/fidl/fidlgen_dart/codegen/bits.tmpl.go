// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const bitsTmpl = `
{{- define "BitsDeclaration" -}}
{{- range .Doc }}
///{{ . -}}
{{- end }}
class {{ .Name }} extends $fidl.Bits {
  factory {{ .Name }}(int _v) {
    {{- if .IsStrict }}
    if ((_v & ~$mask.$value) != 0) {
      throw $fidl.FidlError('Bits value contains unknown bit(s): $_v',
          $fidl.FidlErrorCode.fidlInvalidBit);
    }
    {{- end }}
    return {{ .Name }}._(_v);
  }

{{- range .Members }}
  {{- range .Doc }}
  ///{{ . -}}
  {{- end }}
  static const {{ $.Name }} {{ .Name }} = {{ $.Name }}._({{ .Value }});
{{- end }}
  static const {{ .Name }} $none = {{ .Name }}._(0);
  static const {{ .Name }} $mask = {{ .Name }}._({{ .Mask | printf "%#x" }});

  const {{ .Name }}._(this.$value);

  {{ .Name }} operator |({{ .Name }} other) {
    return {{ .Name }}._($value | other.$value);
  }

  {{ .Name }} operator &({{ .Name }} other) {
    return {{ .Name }}._($value & other.$value);
  }

  {{ .Name }} operator ~() {
    return {{ .Name }}._(~$value & $mask.$value);
  }

  @override
  final int $value;

  @override
  bool hasUnknownBits() {
    return getUnknownBits() != 0;
  }

  @override
  int getUnknownBits() {
    return $value & ~$mask.$value;
  }

  @override
  String toString() {
    List<String> parts = [];
{{- range .Members }}
    if ($value & {{ .Value }} != 0) {
      parts.add(r'{{ $.Name }}.{{ .Name }}');
	  }
{{- end }}
{{- if .IsFlexible }}
    if (hasUnknownBits()) {
      parts.add('0x${getUnknownBits().toRadixString(16)}');
    }
{{- end }}
    if (parts.isEmpty) {
      return r'{{ $.Name }}.$none';
    } else {
      return parts.join(" | ");
    }
  }

  static {{ .Name }} _ctor(int v) => {{ .Name }}(v);
}

const $fidl.BitsType<{{ .Name }}> {{ .TypeSymbol }} = {{ .TypeExpr }};
{{ end }}
`
