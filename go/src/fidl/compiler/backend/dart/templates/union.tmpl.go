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

class {{ .Name }} extends $b.Encodable {
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

  static const int $encodedSize = {{ .EncodedSize }};

  @override
  void $encode($b.Encoder $encoder, int $offset) {
    $encoder.encodeUint32(tag.index, $offset);
    switch (tag) {
{{- range .Members }}
      case {{ $.TagName }}.{{ .Name }}:
        {{ .Type.Encode "_data" .Offset }};
        break;
{{- end }}
      default:
        throw new $b.FidlCodecError('Bad union tag: $tag');
    }
  }

  static {{ .Name }} $decode($b.Decoder $decoder, int $offset) {
    final int $index = $decoder.decodeUint32($offset);
    if ($index >= {{ .TagName }}.values.length)
      throw new $b.FidlCodecError('Bad union tag index: $index');
    switch ({{ .TagName }}.values[$index]) {
{{- range .Members }}
      case {{ $.TagName }}.{{ .Name }}:
        return new {{ $.Name }}.with{{ .Name }}({{ .Type.Decode .Offset }});
{{- end }}
      default:
        throw new $b.FidlCodecError('Bad union tag: $tag');
      }
    }
  }
{{ end }}
`
