// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplBits = `
{{- define "BitsSizeAndAlloc" }}
template<>
struct MinSize<{{ .Natural.Name }}> {
  operator size_t() { return sizeof({{ .Natural.Name }}); }
};
template<>
struct Allocate<{{ .Natural.Name }}> {
  {{ .Natural.Name }} operator()(FuzzInput* src, size_t* size) {
    {{ .Natural.Name }} out;
    ZX_ASSERT(*size >= sizeof({{ .Natural.Name }}));
    ZX_ASSERT(src->CopyObject(&out));
    *size = sizeof({{ .Natural.Name }});
    return out;
  }
};
{{- end }}
`
