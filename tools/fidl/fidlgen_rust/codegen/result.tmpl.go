// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const resultTmpl = `
{{- define "ResultDeclaration" }}
pub type {{ .Name }} = std::result::Result< (
  {{- if gt (len .Ok) 1 }}
    {{- range $ok := .Ok }}
      {{ $ok.Type }},
    {{- end }}
  {{- else }}
    {{- range $ok := .Ok }}
      {{ $ok.Type }}
    {{- end }}
  {{- end }}
), {{ .ErrType }} >;

pub type {{ .Name }}HandleWrapper = std::result::Result< (
  {{- range $ok := .Ok }}
    {{ if $ok.HasHandleMetadata }}
    {{ $ok.HandleWrapperName }}<{{ $ok.Type }}>,
    {{ else }}
    {{ $ok.Type }},
    {{ end }}
  {{- end }}
), {{ .ErrType }} >;
{{- end }}
`
