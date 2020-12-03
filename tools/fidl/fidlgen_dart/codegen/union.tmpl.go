// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const unionTmpl = `
{{- define "UnionDeclaration" -}}
enum {{ .TagName }} {
{{- if .IsFlexible }}
  $unknown,
{{- end }}
{{- range .Members }}
  {{ .Tag }}, // {{ .Ordinal | printf "%#x" }}
{{- end }}
}

const Map<int, {{ .TagName }}> _{{ .TagName }}_map = {
{{- range .Members }}
  {{ .Ordinal }}: {{ $.TagName }}.{{ .Tag }},
{{- end }}
};

{{range .Doc }}
///{{ . -}}
{{- end }}
class {{ .Name }} extends $fidl.XUnion {
{{- range .Members }}

  const {{ $.Name }}.with{{ .CtorName }}({{ .Type.Decl }} value)
    : _ordinal = {{ .Ordinal }}, _data = value;
{{- end }}

{{- if .IsFlexible }}
  const {{ $.Name }}.with$UnknownData(this._ordinal, $fidl.UnknownRawData data)
    : _data = data;
{{- end }}

  {{ .Name }}._(int ordinal, Object data) : _ordinal = ordinal, _data = data;

  final int _ordinal;
  final _data;
{{ if .IsFlexible }}
  {{ .TagName }} get $tag => _{{ .TagName }}_map[_ordinal] ?? {{ .TagName }}.$unknown;
{{- else }}
  {{ .TagName }} get $tag => _{{ .TagName }}_map[_ordinal]!;
{{- end }}

{{range .Members }}
  {{ .Type.OptionalDecl }} get {{ .Name }} {
    if (_ordinal != {{ .Ordinal }}) {
      return null;
    }
    return _data;
  }

{{- end }}

  $fidl.UnknownRawData? get $unknownData {
    switch (_ordinal) {
{{- range .Members }}
    case {{ .Ordinal }}:
{{- end }}
      return null;
    default:
      return _data;
    }
  }

  @override
  String toString() {
    switch (_ordinal) {
{{- range .Members }}
      case {{ .Ordinal }}:
        return r'{{ $.Name }}.{{ .Name }}(' + {{ .Name }}.toString() +')';
{{- end }}
      default:
        return r'{{ $.Name }}.<UNKNOWN>';
    }
  }

  @override
  int get $ordinal => _ordinal;

  @override
  Object get $data => _data;

  static {{ .Name }} _ctor(int ordinal, Object data) {
    return {{ .Name }}._(ordinal, data);
  }
}

// See fxbug.dev/7644:
// ignore: recursive_compile_time_constant
const $fidl.UnionType<{{ .Name }}> {{ .TypeSymbol }} = {{ .TypeExpr }};
// See fxbug.dev/7644:
// ignore: recursive_compile_time_constant
const $fidl.NullableUnionType<{{ .Name }}> {{ .OptTypeSymbol }} = {{ .OptTypeExpr }};
{{ end }}
`
