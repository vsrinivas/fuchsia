// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplEnum = `
{{- define "EnumSizeAndAlloc" }}
template<>
struct MinSize<{{ .Natural.Name }}> {
  operator size_t() { return sizeof(uint64_t); }
};
template<>
struct Allocate<{{ .Natural.Name }}> {
  {{ .Natural.Name }} operator()(FuzzInput* src, size_t* size) {
    {{ .Natural.Name }} out;
    uint64_t selector;
    ZX_ASSERT(*size >= sizeof(uint64_t));
    ZX_ASSERT(src->CopyObject(&selector));
    *size = sizeof(uint64_t);

    switch (selector % {{ len .Members }}) {
    {{- $enumName := .Natural.Name }}
    {{- range $memberIdx, $member := .Members }}
      case {{ $memberIdx }}:
        out = {{ $enumName }}::{{ $member.Name }};
        break;
    {{- end }}
    }

    return out;
  }
};
{{- end }}
`
