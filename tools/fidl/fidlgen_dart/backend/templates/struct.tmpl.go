// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

// Struct is the template for struct declarations.
const Struct = `
{{- define "StructDeclaration" -}}
{{- range .Doc }}
///{{ . -}}
{{- end }}
class {{ .Name }} extends $fidl.Struct {
  const {{ .Name }}({
{{- range .Members }}
    {{ if not .Type.Nullable }}{{ if not .DefaultValue }}required {{ end }}{{ end -}}
    this.{{ .Name }}{{ if .DefaultValue }}: {{ .DefaultValue }}{{ end }},
{{- end }}
  });
  {{ .Name }}.clone({{ .Name }} $orig, {
{{- range .Members }}
  {{ .Type.OptionalDecl }} {{ .Name }},
{{- end }}
  }) : this(
    {{- range .Members }}
      {{ .Name }}: {{ .Name }} ?? $orig.{{ .Name }},
    {{- end }}
    );


  {{ if .HasNullableField }}
    {{ .Name }}.cloneWithout({{ .Name }} $orig, {
      {{- range .Members }}
        {{ if .Type.Nullable }}bool {{ .Name }}=false,{{ end }}
      {{- end }}
    }) : this(
      {{- range .Members }}
        {{ if .Type.Nullable }}
          {{ .Name }}: {{ .Name }} ? null : $orig.{{ .Name }},
        {{ else }}
          {{ .Name }}: $orig.{{ .Name }},
        {{ end }}
      {{- end }}
      );
  {{ end }}

{{- range .Members }}
  {{- range .Doc }}
  ///{{ . -}}
  {{- end }}
  final {{ .Type.Decl }} {{ .Name }};
{{- end }}

  @override
  List<Object?> get $fields {
    return <Object?>[
  {{- range .Members }}
      {{ .Name }},
  {{- end }}
    ];
  }

  {{- range $index, $member := .Members }}
  static const $fieldType{{ $index}} = {{ $member.TypeSymbol }};
  {{- end }}

  @override
  void $encode($fidl.Encoder $encoder, int $offset) {
    {{- range $index, $member := .Members }}
    $fieldType{{ $index }}.encode($encoder, {{ $member.Name }}, $offset + {{ $member.OffsetV1 }});
    {{- end }}
  }

  @override
  String toString() {
    return r'{{ .Name }}' r'(
{{- range $index, $member := .Members -}}
      {{- if $index }}, {{ end -}}{{ $member.Name  }}: ' + {{ $member.Name }}.toString() + r'
{{- end -}}
    )';
  }

  static {{ .Name }} _structDecode($fidl.Decoder $decoder, int $offset) {
    return {{ .Name }}(
    {{ range $index, $member := .Members }}
    {{- if ne $index 0 }},{{ end }}
    {{ $member.Name }}: $fieldType{{ $index }}.decode($decoder, $offset + {{ $member.OffsetV1 }})
    {{- end -}}
    );
  }
}

// See fxbug.dev/7644:
// ignore: recursive_compile_time_constant
const $fidl.StructType<{{ .Name }}> {{ .TypeSymbol }} = {{ .TypeExpr }};
{{ end }}
`
