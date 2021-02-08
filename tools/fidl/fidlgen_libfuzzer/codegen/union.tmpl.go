// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplUnion = `
{{- define "UnionSizeAndAlloc" }}
template<>
struct MinSize<{{ .Decl.Natural.Name }}> {
  operator size_t() {
    size_t sizes[] = {0{{ range  .Members }}, MinSize<{{ .Type.Natural }}>(){{ end }}};
    return 1 + *std::max_element(sizes, sizes + {{ len .Members }} + 1);
  }
};
template<>
struct Allocate<{{ .Decl.Natural.Name }}> {
  static_assert({{ len .Members }} > 0, "xunion must have at least one member");

  {{ .Decl.Natural.Name }} operator()(FuzzInput* src, size_t* size) {
    ZX_ASSERT(*size >= MinSize<{{ .Decl.Natural.Name }}>());

    uint8_t selector;
    ZX_ASSERT(src->CopyBytes(&selector, 1));
    (*size)++;

    {{ .Decl.Natural.Name }} out;
    switch (selector % {{ len .Members }}) {
      {{- range $memberIdx, $member := .Members }}
      case {{ $memberIdx }}: {
        out.set_{{ $member.Name }}(Allocate<{{ $member.Type.Natural }}>{}(src, size));
        break;
      }
      {{- end }}
    }

    return out;
  }
};
{{- end }}
`
