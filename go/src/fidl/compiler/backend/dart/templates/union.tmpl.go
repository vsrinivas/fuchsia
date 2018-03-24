// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionDeclaration" -}}
enum {{ .TagName }} {
{{- range .Members }}
  {{ .Name }},
{{- end }}
}

class {{ .Name }} extends $fidl.Union {
{{- range .Members }}

  const {{ $.Name }}.with{{ .CtorName }}({{ .Type.Decl }} value)
    : _data = value, tag = {{ $.TagName }}.{{ .Name }};
{{- end }}

  {{ .Name }}._(this.tag, Object data) : _data = data;

  final {{ .TagName }} tag;
  final _data;

{{- range .Members }}
  {{ .Type.Decl }} get {{ .Name }} {
    if (tag != {{ $.TagName }}.{{ .Name }})
      return null;
    return _data;
  }
{{- end }}

  @override
  String toString() {
    switch (tag) {
{{- range .Members }}
      case {{ $.TagName }}.{{ .Name }}:
        return '{{ $.Name }}.{{ .Name }}(${{ .Name }})';
{{- end }}
      default:
        return null;
    }
  }

  @override
  int get $index => tag.index;

  @override
  Object get $data => _data;

  static {{ .Name }} _ctor(int index, Object data) {
    return new {{ .Name }}._({{ .TagName }}.values[index], data);
  }
}

const $fidl.UnionType<{{ .Name }}> {{ .TypeSymbol }} = {{ .TypeExpr }};
{{ end }}
`
