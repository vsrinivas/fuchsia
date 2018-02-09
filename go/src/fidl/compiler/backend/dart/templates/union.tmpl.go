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

class {{ .Name }} extends Encodable {
  {{- range .Members }}

  const {{ $.Name }}.with{{ .Name }}({{ .Type.Decl }} value)
    : _data = value, tag = {{ $.TagName }}.{{ .Name }};
  {{- end }}

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
        return "{{ $.Name }}.{{ .Name }}(${{ .Name }})";
  {{- end }}
      default:
        return null;
    }
  }

  dynamic toJson() => _data.toJson();

  @override
  int get encodedSize => {{ .EncodedSize }};

  @override
  void encode(Encoder encoder, int offset) {
    encoder.encodeUint32(tag.index, offset);
    switch (tag) {
  {{- range .Members }}
      case {{ $.TagName }}.{{ .Name }}:
        {{ .Type.Encode .Name .Offset }};
        break;
  {{- end }}
      default:
        throw new FidlCodecError('Bad union tag: $tag');
    }
  }
}
{{ end }}
`
