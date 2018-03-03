// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" -}}
class {{ .Name }} extends $fidl.Struct {
  const {{ .Name }}({
{{- range .Members }}
    {{ if not .Type.Nullable }}@required {{ end }}this.{{ .Name }},
{{- end }}
  });

  {{ .Name }}._(List<Object> argv)
    :
{{- range $index, $member := .Members -}}
  {{- if $index }},
      {{ else }} {{ end -}}
    {{ .Name }} = argv[{{ $index }}]
{{- end }};

{{- range .Members }}
  final {{ .Type.Decl }} {{ .Name }};
{{- end }}

  @override
  List<Object> get $fields {
    return <Object>[
  {{- range .Members }}
      {{ .Name }},
  {{- end }}
    ];
  }

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

  static {{ .Name }} _ctor(List<Object> argv) => new {{ .Name }}._(argv);
}

const $fidl.StructType<{{ .Name }}> {{ .TypeSymbol }} = {{ .TypeExpr }};
{{ end }}
`
