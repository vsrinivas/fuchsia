// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Enum = `
{{- define "EnumDeclaration" -}}
{{- $enum := . }}
fidl_enum! {
  {{ $enum.Name }}({{ $enum.Type }}) {
    {{- range $member :=  $enum.Members }}
    {{ $member.Name }} = {{ $member.Value }},
  {{- end }}
  }
}
{{ end }}
`
