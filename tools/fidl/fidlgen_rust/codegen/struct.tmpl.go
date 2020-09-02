// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const structTmpl = `
{{- define "StructDeclaration" }}

{{- if .Members }}

{{- range .DocComments}}
///{{ . }}
{{- end}}
{{ .Derives }}
{{ if .UseFidlStructCopy -}}
#[repr(C)]
{{- end }}
pub struct {{ .Name }} {
  {{- range .Members }}
  {{- range .DocComments}}
  ///{{ . }}
  {{- end}}
  pub {{ .Name }}: {{ .Type }},
  {{- end }}
}

{{ if .UseFidlStructCopy -}}
fidl_struct_copy! {
{{- else -}}
fidl_struct! {
{{- end }}
  name: {{ .Name }},
  members: [
  {{- range .Members }}
    {{ .Name }} {
      ty: {{ .Type }},
      offset_v1: {{ .Offset }},
    },
  {{- end }}
  ],
  padding: [
  {{- if .UseFidlStructCopy -}}
  {{- range .FlattenedPaddingMarkers }}
  {
      ty: {{ .Type }},
      offset: {{ .Offset }},
      mask: {{ .Mask }},
  },
  {{- end }}
  {{- else -}}
  {{- range .PaddingMarkers }}
  {
      ty: {{ .Type }},
      offset: {{ .Offset }},
      mask: {{ .Mask }},
  },
  {{- end }}
  {{- end -}}
  ],
  size_v1: {{ .Size }},
  align_v1: {{ .Alignment }},
}
{{- else }}

{{- range .DocComments}}
///{{ . }}
{{- end}}
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct {{ .Name }};

fidl_empty_struct!({{ .Name }});

{{- end }}
{{- end }}
`
