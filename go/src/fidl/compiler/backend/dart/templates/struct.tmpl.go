// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" -}}
class {{ .Name }} extends $b.Encodable {

{{- range .Members }}
  final {{ .Type.Decl }} {{ .Name }};
{{- end }}

  const {{ .Name }}({
{{- range .Members }}
  {{- if not .Type.Nullable }}
    @required
  {{- end }}
    this.{{ .Name }},
{{- end }}
  });

  @override
  String toString() {
    return "{{ .Name }}(
{{- range $index, $member := .Members -}}
      {{- if $index }}, {{ end -}}{{ $member.Name  }}: ${{ $member.Name  }}
{{- end -}}
    )";
  }

  Map toJson() {
    final Map $map = new Map();
{{- range .Members }}
    $map["{{ .Name }}"] = {{ .Name }};
{{- end }}
    return $map;
  }

  static const int $encodedSize = {{ .EncodedSize }};

  @override
  void $encode($b.Encoder $encoder, int $offset) {
  {{- range .Members }}
    {{ .Type.Encode .Name .Offset }};
  {{- end }}
  }

  static {{ .Name }} $decode($b.Decoder $decoder, int $offset) {
    return new {{ .Name }}(
  {{- range .Members }}
      {{ .Name }}: {{ .Type.Decode .Offset }},
  {{- end }}
    );
  }
}
{{ end }}
`
