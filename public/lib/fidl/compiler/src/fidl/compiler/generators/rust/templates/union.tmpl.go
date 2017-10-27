// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateUnion = `
{{/* . (dot) refers to a the Go type |rustgen.UnionTemplate|  */}}
{{- define "GenerateUnion" -}}
{{- $union := . -}}

// -- {{$union.Name}} --

{{template "GenerateEnum" $union.TagsEnum}}

pub enum {{$union.Name}} {
{{range $field := $union.Fields}}    {{$field.Name}}({{$field.Type}}),
{{end -}}
}

impl ::fidl::Encodable for {{$union.Name}} {
    fn encode(self, buf: &mut ::fidl::EncodeBuf, base: usize, offset: usize) {
        let start = base + offset;
        ::fidl::Encodable::encode(16u32, buf, start, 0);
        match self {
{{range $field := $union.Fields}}            {{$union.Name}}::{{$field.Name}}(val) => {
  ::fidl::Encodable::encode({{$union.TagsEnum.Name}}::{{$field.Name}}, buf, start, 4);
                ::fidl::{{if $field.IsUnion}}CodableUnion::encode_
                    {{- if $field.IsNullable}}opt_{{end}}as_ptr(val, buf, start + 8)
                    {{- else -}}Encodable::encode(val, buf, start + 8, 0){{end -}}
                    ;
            },
{{end}}        }
    }

    fn encodable_type() -> ::fidl::EncodableType {
        ::fidl::EncodableType::Union
    }

    fn size() -> usize {
        16
    }
}

impl ::fidl::Decodable for {{$union.Name}} {
    fn decode(buf: &mut ::fidl::DecodeBuf, base: usize, offset: usize) -> ::fidl::Result<Self> {
        let start = base + offset;
        let size: u32 = ::fidl::Decodable::decode(buf, start, 0).unwrap();
        if size != 16 { return Err(::fidl::Error::Invalid); }
        let tag: {{$union.TagsEnum.Name}} = ::fidl::Decodable::decode(buf, start, 4).unwrap();
        match tag {
			  {{range $field := $union.Fields}}            {{$union.TagsEnum.Name}}::{{$field.Name}} =>
                ::fidl::
                    {{- if $field.IsUnion}}CodableUnion::decode_
                    {{- if $field.IsNullable}}opt_{{end}}as_ptr(buf, start + 8)
                    {{- else -}}Decodable::decode(buf, start + 8, 0){{end -}}
                    .map({{$union.Name}}::{{$field.Name}}),
{{end}}            _ => Err(::fidl::Error::UnknownUnionTag),
        }
    }
}

impl_codable_union!({{$union.Name}});
{{end}}
`
