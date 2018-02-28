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

const $fidl.UnionType {{ .TypeSymbol }} = {{ .TypeExpr }};

class {{ .Name }} extends $fidl.Encodable {
{{- range .Members }}

  const {{ $.Name }}.with{{ .Name }}({{ .Type.Decl }} value)
    : _data = value, tag = {{ $.TagName }}.{{ .Name }};
{{- end }}

  factory {{ .Name }}.$decode($fidl.Decoder $decoder, int $offset) {
    final int $index = $decoder.decodeUint32($offset);
    if ($index >= {{ .TagName }}.values.length)
      throw new $fidl.FidlError('Bad union tag index: ${$index}');
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.members;
    switch ({{ .TagName }}.values[$index]) {
{{- range $index, $member := .Members }}
      case {{ $.TagName }}.{{ .Name }}:
        return new {{ $.Name }}.with{{ .Name }}({{ .Type.Decode .Offset $index }});
{{- end }}
      default:
        throw new $fidl.FidlError('Bad union tag: ${ {{ .TagName }}.values[$index] }');
    }
  }

  final _data;
  final {{ .TagName }} tag;

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

  dynamic toJson() => _data.toJson();

  @override
  void $encode($fidl.Encoder $encoder, int $offset, $fidl.FidlType type) {
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.members;
    $encoder.encodeUint32(tag.index, $offset);
    switch (tag) {
{{- range $index, $member := .Members }}
      case {{ $.TagName }}.{{ .Name }}:
        {{ .Type.Encode "_data" .Offset $index }};
        break;
{{- end }}
      default:
        throw new $fidl.FidlError('Bad union tag: $tag');
    }
  }
}
{{ end }}
`
