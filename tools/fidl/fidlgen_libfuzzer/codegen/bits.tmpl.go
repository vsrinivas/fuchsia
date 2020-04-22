// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplBits = `
{{- define "BitsSizeAndAlloc" }}
template<>
struct MinSize<{{ .Name }}> {
  operator size_t() { return sizeof({{ .Name }}); }
};
template<>
struct Allocate<{{ .Name }}> {
  {{ .Name }} operator()(FuzzInput* src, size_t* size) {
    {{ .Name }} out;
    ZX_ASSERT(*size >= sizeof({{ .Name }}));
    ZX_ASSERT(src->CopyObject(&out));
    *size = sizeof({{ .Name }});
    return out;
  }
};
{{- end }}
`
