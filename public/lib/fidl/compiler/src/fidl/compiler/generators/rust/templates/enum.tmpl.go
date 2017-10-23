// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateEnum = `
{{/* . (dot) refers to a the Go type |rustgen.EnumTemplate|  */}}
{{- define "GenerateEnum" -}}
{{- $enum := . -}}
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct {{$enum.Name}}(
  {{- if eq $enum.Signed true}} i32
  {{- else}} u32 {{- end}}
);

impl {{$enum.Name}} {
   {{range $enum_val := $enum.Values}}
      pub const {{$enum_val.Name}}: {{$enum.Name}} = {{$enum.Name}}({{$enum_val.Value}});
  {{end}}

  {{if eq $enum.Signed true -}}
     pub const UNKNOWN: {{$enum.Name}} = {{$enum.Name}}(0x7FFFFFFF);
  {{else}}
     pub const UNKNOWN: {{$enum.Name}} = {{$enum.Name}}(0xFFFFFFFF);
  {{end}}
}

impl ::fidl::Encodable for {{$enum.Name}} {
  fn encode(self, buf: &mut ::fidl::EncodeBuf, base: usize, offset: usize) {
    ::fidl::Encodable::encode(self.0, buf, base, offset)
  }

  fn encodable_type() -> ::fidl::EncodableType {
    <
    {{- if eq $enum.Signed true}} i32
    {{- else}} u32 {{- end}}
    as ::fidl::Encodable>::encodable_type()
  }

  fn size() -> usize {
    <
    {{- if eq $enum.Signed true}} i32
    {{- else}} u32 {{- end}}
    as ::fidl::Encodable>::size()
  }
}

impl ::fidl::Decodable for {{$enum.Name}} {
  fn decode(buf: &mut ::fidl::DecodeBuf, base: usize, offset: usize) -> ::fidl::Result<Self> {
      Ok({{$enum.Name}}(::fidl::Decodable::decode(buf, base, offset)?))
  }
}

{{end}}`
