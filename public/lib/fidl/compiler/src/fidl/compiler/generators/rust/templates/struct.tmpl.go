// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateStruct = `
{{- /* . (dot) refers to the Go type |rustgen.StructTemplate| */ -}}
{{- define "GenerateStruct" -}}
{{- $struct := . -}}

// -- {{$struct.Name}} --

// Constants
{{range $const := $struct.Constants -}}
pub const {{$const.Name}}: {{$const.Type}} = {{$const.Value}};
{{end -}}

// Enums
{{range $enum := $struct.Enums -}}
{{template "GenerateEnum" $enum}}
{{end -}}

// Struct version information
{{$versions := len $struct.Versions -}}
const {{$struct.Name}}Versions: [(u32, u32); {{$versions}}] = [
{{range $version := $struct.Versions}}    ({{$version.Version}}, {{$version.Size}}),
{{end -}}
];

// Struct definition
#[derive(Debug)]
pub struct {{$struct.Name}} {
{{range $field := $struct.Fields}}    pub {{$field.Name}}: {{$field.Type}},
{{end -}}
}

impl ::fidl::EncodablePtr for {{$struct.Name}} {
    fn body_size(&self) -> usize {
        {{$struct.Size}}
    }

    fn header_data(&self) -> u32 {
        {{$struct.Version}}
    }

    fn encode_body(self, buf: &mut ::fidl::EncodeBuf, base: usize) {
{{range $field := $struct.Fields}}        ::fidl::Encodable::encode(self.{{$field.Name}}, buf, base, {{$field.Offset}});
{{end}}    }
}

// TODO(raph): more version negotiation
impl ::fidl::DecodablePtr for {{$struct.Name}} {
    fn decode_body(buf: &mut ::fidl::DecodeBuf, size: u32, val: u32, base: usize) -> ::fidl::Result<Self> {
        if size < {{$struct.Size}} { return Err(::fidl::Error::OutOfRange); }
        Ok({{$struct.Name}} {
{{range $field := $struct.Fields}}            {{$field.Name}}: try!(::fidl::Decodable::decode(buf, base, {{$field.Offset}})),
{{end}}        })
    }
}

impl_codable_ptr!({{$struct.Name}});
{{end}}
`
