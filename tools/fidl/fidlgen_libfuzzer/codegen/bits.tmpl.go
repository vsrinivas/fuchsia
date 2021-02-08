// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplBits = `
{{- define "BitsSizeAndAlloc" }}
template<>
struct MinSize<{{ .Decl.Natural.Name }}> {
  operator size_t() { return sizeof({{ .Decl.Natural.Name }}); }
};
template<>
struct Allocate<{{ .Decl.Natural.Name }}> {
  {{ .Decl.Natural.Name }} operator()(FuzzInput* src, size_t* size) {
    {{ .Decl.Natural.Name }} out;
    ZX_ASSERT(*size >= sizeof({{ .Decl.Natural.Name }}));
    ZX_ASSERT(src->CopyObject(&out));
    *size = sizeof({{ .Decl.Natural.Name }});
    return out;
  }
};
{{- end }}
`
