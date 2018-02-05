// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionForwardDeclaration" -}}
struct {{ .Name }};
{{- end }}

{{- define "UnionDeclaration" }}
struct {{ .Name }} {
  ::fidl_union_tag_t tag;
  union {
    {{- range .Members }}
    {{ .Name|.Type.Decorate }};
    {{- end }}
  };
};
{{- end }}
`
