// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Bits = `
{{- define "BitsDeclaration" -}}
{{- $bits := . }}
fidl_bits! {
  {{ $bits.Name }}({{ $bits.Type.Decl }}) {
    {{- range $member :=  $bits.Members }}
    {{ $member.Name }} = {{ $member.Value }},
  {{- end }}
  }
}
{{ end }}
`
