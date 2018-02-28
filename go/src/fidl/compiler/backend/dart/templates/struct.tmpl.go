// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" -}}
const $fidl.StructType {{ .TypeSymbol }} = {{ .TypeExpr }};

class {{ .Name }} extends $fidl.Encodable {
  const {{ .Name }}({
{{- range .Members }}
  {{- if not .Type.Nullable }}
    @required
  {{- end }}
    this.{{ .Name }},
{{- end }}
  });

  factory {{ .Name }}.$decode($fidl.Decoder $decoder, int $offset) {
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.members;
    return new {{ .Name }}(
{{- range $index, $member := .Members }}
      {{ .Name }}: {{ .Type.Decode .Offset $index }},
{{- end }}
    );
  }

{{- range .Members }}
  final {{ .Type.Decl }} {{ .Name }};
{{- end }}

  @override
  String toString() {
    return '{{ .Name }}(
{{- range $index, $member := .Members -}}
      {{- if $index }}, {{ end -}}{{ $member.Name  }}: ${{ $member.Name  }}
{{- end -}}
    )';
  }

  Map toJson() {
    return {
{{- range .Members }}
     '{{ .Name }}': {{ .Name }},
{{- end }}
    };
  }

  @override
  void $encode($fidl.Encoder $encoder, int $offset, $fidl.FidlType type) {
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.members;
{{- range $index, $member := .Members }}
    {{ .Type.Encode .Name .Offset $index }};
{{- end }}
  }
}
{{ end }}
`
