// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplStruct = `
{{- define "StructSizeAndAlloc" }}
template<>
struct MinSize<{{ .Natural.Name }}> {
  operator size_t() {
    return {{ range $index, $member := .Members }}
      {{- if $index }} + {{ end }}MinSize<{{ $member.Type.Natural }}>()
    {{- end }};
  }
};
template<>
struct Allocate<{{ .Natural.Name }}> {
  {{ .Natural.Name }} operator()(FuzzInput* src, size_t* size) {
    ZX_ASSERT(*size >= MinSize<{{ .Natural.Name }}>());
    const size_t slack_per_member = (*size - MinSize<{{ .Natural.Name }}>()) / {{ len .Members }};
    {{ .Natural.Name }} out;
    size_t out_size;
    {{- range .Members }}
    out_size = MinSize<{{ .Type.Natural }}>() + slack_per_member;
    out.{{ .Name }} = Allocate<{{ .Type.Natural }}>{}(src, &out_size);
    {{- end }}
    return out;
  }
};
{{- end }}
`
