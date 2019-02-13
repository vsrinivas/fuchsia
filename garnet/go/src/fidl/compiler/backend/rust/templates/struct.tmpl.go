// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" }}
{{- if not .LargeArrays }}
#[derive(Debug, PartialEq)]
{{- end }}
{{- range .DocComments}}
///{{ . }}
{{- end}}

{{- if .Members }}
pub struct {{ .Name }} {
  {{- range .Members }}
  {{- range .DocComments}}
  ///{{ . }}
  {{- end}}
  pub {{ .Name }}: {{ .Type }},
  {{- end }}
}

fidl_struct! {
  name: {{ .Name }},
  members: [
  {{- range .Members }}
    {{ .Name }} {
      ty: {{ .Type }},
      offset: {{ .Offset }},
    },
  {{- end }}
  ],
  size: {{ .Size }},
  align: {{ .Alignment }},
}
{{- else }}
pub struct {{ .Name }};

impl fidl::encoding::Encodable for {{ .Name }} {
  fn inline_align(&self) -> usize { 1 }
  fn inline_size(&self) -> usize { 1 }
  fn encode(&mut self, encoder: &mut fidl::encoding::Encoder) -> fidl::Result<()> {
	  ::fidl::fidl_encode!(&mut 0u8, encoder)
  }
}

impl fidl::encoding::Decodable for {{ .Name }} {
  fn inline_align() -> usize { 1 }
  fn inline_size() -> usize { 1 }
  fn new_empty() -> Self { {{ .Name }} }
  fn decode(&mut self, decoder: &mut fidl::encoding::Decoder) -> fidl::Result<()> {
    let mut x = 0u8;
	 ::fidl::fidl_decode!(&mut x, decoder)?;
    if x == 0 {
		 Ok(())
    } else {
		 Err(::fidl::Error::Invalid)
    }
  }
}

impl fidl::encoding::Autonull for {{ .Name }} {}
{{- end }}
{{- end }}
`
