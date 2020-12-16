// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tableTmpl = `
{{- define "TableDeclaration" }}
{{- range .DocComments}}
///{{ . }}
{{- end}}
{{ .Derives }}
pub struct {{ .Name }} {
  {{- range .Members }}
  {{- range .DocComments}}
  ///{{ . }}
  {{- end}}
  pub {{ .Name }}: Option<{{ .Type }}>,
  {{- end }}
  /// (FIDL-generated) Unknown fields encountered during decoding, stored as a
  /// map from ordinals to raw data. The ` + "`Some`" + ` case is always nonempty.
  pub unknown_data: Option<std::collections::BTreeMap<u64, {{ if .IsResourceType }}fidl::UnknownData{{ else }}Vec<u8>{{ end }}>>,
  #[deprecated = "Use ` + "`..{{ .Name }}::EMPTY` to construct and `..`" + ` to match."]
  #[doc(hidden)]
  pub __non_exhaustive: (),
}

fidl_table! {
  name: {{ .Name }},
  members: [
    {{- range .Members }}
    {{ .Name }} {
      ty: {{ .Type }},
      ordinal: {{ .Ordinal }},
      {{- if .HasHandleMetadata }}
      handle_metadata: {
        handle_subtype: {{ .HandleSubtype }},
        handle_rights: {{ .HandleRights }},
      },
      {{- end }}
    },
    {{- end }}
  ],
  {{ if .IsResourceType }}resource{{ else }}value{{ end }}_unknown_member: unknown_data,
}
{{- end }}
`
