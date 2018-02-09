// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Enum = `
{{- define "EnumDeclaration" -}}
class {{ .Name }} extends Encodable {
  {{- range .Members }}
  static const {{ $.Name }} {{ .Name }} = const {{ $.Name }}._({{ .Value }});
  {{- end }}

  const {{ .Name }}._(this.value);

  final int value;

  static const Map<String, {{ .Name }}> valuesMap = const {
  {{- range .Members }}
    "{{ .Name }}": {{ .Name }},
  {{- end }}
  };

  static const List<{{ .Name }}> values = const [
    {{- range .Members }}
    {{ .Name }},
    {{- end }}
  ];

  static {{ .Name }} valueOf(String name) => valuesMap[name];

  factory {{ .Name }}(int v) {
    switch (v) {
  {{- range .Members }}
      case {{ .Value }}:
        return {{ .Name }};
  {{- end }}
      default:
        return null;
    }
  }

  @override
  String toString() {
    switch (value) {
  {{- range .Members }}
      case {{ .Value }}:
        return '{{ $.Name }}.{{ .Name }}';
  {{- end }}
      default:
        return null;
    }
  }

  int toJson() => value;

  @override
  int get encodedSize => {{ .EncodedSize }};

  @override
  void encode(Encoder encoder, int offset) {
    encoder.encode{{ .CodecSuffix }}(value, offset);
  }
}
{{ end }}
`
