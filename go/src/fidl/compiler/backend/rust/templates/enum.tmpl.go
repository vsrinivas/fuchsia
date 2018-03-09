// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Enum = `
{{- define "EnumDeclaration" -}}
{{- $enum := . }}
fidl2_enum! {
  {{ $enum.Name }}({{ $enum.Type }}) {
    {{- range $member :=  $enum.Members }}
    {{ $member.Name }} = {{ $member.Value }},
  {{- end }}
  }
}

// TODO(cramertj) do we need these? It seems like some places assume
// these names are available at top-level (example-9), but
// adding them to the top-level causes clashes in the "enums" example.
{{- range $member := $enum.Members }}
pub const {{ $member.ConstName }}: {{ $enum.Name }} = {{ $enum.Name }}::{{ $member.Name }};
{{- end }}
{{ end }}
`
