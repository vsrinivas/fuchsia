// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const resultTmpl = `
{{- define "ResultDeclaration" }}
pub type {{ .Name }} = std::result::Result< (
  {{- if gt (len .Ok) 1 }}
    {{- range $ok_member_type := .Ok }}
      {{ $ok_member_type }},
    {{- end }}
  {{- else }}
    {{- range $ok_member_type := .Ok }}
      {{ $ok_member_type }}
    {{- end }}
  {{- end }}
), {{ .ErrType }} >;
{{- end }}
`
