// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

// Enum is the template for enum declarations.
const Enum = `
{{- define "EnumDeclaration" -}}
{{- range .Doc }}
///{{ . -}}
{{- end }}
class {{ .Name }} extends $fidl.Enum {
{{ if .IsFlexible }}
  static final Map<int, {{ .Name }}> _internedValues = {
{{- range .Members }}
    {{ .Value }}: {{ .Name }},
{{- end }}
  };
{{- end }}
  factory {{ .Name }}(int _v) {
{{ if .IsStrict }}
    switch (_v) {
{{- range .Members }}
      case {{ .Value }}:
        return {{ .Name }};
{{- end }}
      default:
        throw $fidl.FidlError('Invalid strict enum value: $_v',
          $fidl.FidlErrorCode.fidlInvalidEnumValue);
    }
{{ else }}
    if (!_internedValues.containsKey(_v)) {
      _internedValues[_v] = {{ .Name }}._(_v, true);
    }
    return _internedValues[_v];
{{ end }}
  }

{{- range .Members }}
  {{- range .Doc }}
  ///{{ . -}}
  {{- end }}
  static const {{ $.Name }} {{ .Name }} = {{ $.Name }}._({{ .Value }}{{ if $.IsFlexible }}, {{ .IsUnknown }}{{ end }});
{{- end }}
{{ if .IsFlexible }}
  /// Default unknown placeholder.
  static const {{ $.Name }} $unknown = {{ $.Name }}._({{ .UnknownValueForTmpl | printf "%#x" }}, true);
{{ end }}

{{ if .IsStrict }}
  const {{ .Name }}._(this.$value);
{{ else }}
  const {{ .Name }}._(this.$value, this._isUnknown);
{{ end }}

  @override
  final int $value;
{{ if .IsFlexible }}
  final bool _isUnknown;
{{ end }}

  static const Map<String, {{ .Name }}> $valuesMap = {
  {{- range .Members }}
    r'{{ .Name }}': {{ .Name }},
  {{- end }}
  };

  static const List<{{ .Name }}> $values = [
    {{- range .Members }}
    {{ .Name }},
    {{- end }}
  ];

  static {{ .Name }} $valueOf(String name) => $valuesMap[name];

  @override
  bool isUnknown() {
{{ if .IsStrict }}
    return false;
{{ else }}
    return _isUnknown;
{{ end }}
  }

  @override
  String toString() {
    switch ($value) {
  {{- range .Members }}
      case {{ .Value }}:
        return r'{{ $.Name }}.{{ .Name }}';
  {{- end }}
      default:
        return r'{{ $.Name }}.' '${$value}';
    }
  }

  static {{ .Name }} _ctor(int v) => {{ .Name }}(v);
}

const $fidl.EnumType<{{ .Name }}> {{ .TypeSymbol }} = {{ .TypeExpr }};
{{ end }}
`
