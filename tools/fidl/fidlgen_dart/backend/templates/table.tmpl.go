// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

// Table is the template for struct declarations.
const Table = `
{{- define "TableDeclaration" -}}
{{- range .Doc }}
///{{ . -}}
{{- end }}
class {{ .Name }} extends $fidl.Table {
  const {{ .Name }}({
    this.$unknownData,
{{- range .Members }}
    this.{{ .Name }}{{ if .DefaultValue }}: {{ .DefaultValue }}{{ end }},
{{- end }}
  });

  {{ .Name }}._(Map<int, dynamic> argv, this.$unknownData)
    {{- if len .Members }}:
{{- range $index, $member := .Members -}}
  {{- if $index }},
      {{ else }} {{ end -}}
    {{ .Name }} = argv[{{ .Ordinal }}]
{{- end }}{{- end }};

  @override
  final Map<int, $fidl.UnknownRawData>? $unknownData;
{{- range .Members }}
  {{- range .Doc }}
  ///{{ . -}}
  {{- end }}
  final {{ .Type.OptionalDecl }} {{ .Name }};
{{- end }}

  @override
  dynamic $field(int index) {
    switch (index) {
      {{- range .Members }}
          case {{ .Index }}:
            return {{ .Name }};
      {{- end }}
    }
    return null;
  }

  @override
  Map<int, dynamic> get $fields {
    return {
  {{- range .Members }}
      {{ .Ordinal }}: {{ .Name }},
  {{- end }}
    };
  }

  static {{ .Name }} _ctor(Map<int, dynamic> argv,
      [Map<int, $fidl.UnknownRawData>? unknownData]) =>
    {{ .Name }}._(argv, unknownData);
}

// See fxbug.dev/7644:
// ignore: recursive_compile_time_constant
const $fidl.TableType<{{ .Name }}> {{ .TypeSymbol }} = {{ .TypeExpr }};
{{ end }}
`
