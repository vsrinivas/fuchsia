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
{{end}}    _Unknown(u64),
}

impl MojomUnion for {{$union.Name}} {
    fn get_tag(&self) -> u32 {
        match *self {
{{range $field := $union.Fields}}            {{$union.Name}}::{{$field.Name}}(_) => {{$union.TagsEnum.Name}}_{{$field.Name}},
{{end}}            {{$union.Name}}::_Unknown(_) => {{$union.TagsEnum.Name}}__UNKNOWN,
        }
    }
    fn encode_value(self, encoder: &mut Encoder, context: Context) {
        match self {
{{range $field := $union.Fields}}            {{$union.Name}}::{{$field.Name}}(val) => MojomEncodable::encode(val, encoder, context.clone()),
{{end}}            {{$union.Name}}::_Unknown(val) => MojomEncodable::encode(val, encoder, context.clone()),
        }
    }
    fn decode_value(decoder: &mut Decoder, context: Context) -> Result<Self, ValidationError> {
        let tag = {
            let mut state = decoder.get_mut(&context);
            let bytes = state.decode::<u32>();
            if (bytes as usize) != UNION_SIZE {
                return Err(ValidationError::UnexpectedNullUnion);
            }
	    state.decode::<u32>()
        };
        Ok(match tag {
{{range $field := $union.Fields}}            {{$union.TagsEnum.Name}}_{{$field.Name}} => {{$union.Name}}::{{$field.Name}}({
                match <{{$field.Type}}>::decode(decoder, context.clone()) {
                    Ok(value) => value,
                    Err(err) => return Err(err),
                }
            }),
{{end}}        _ => {{$union.Name}}::_Unknown(u64::decode(decoder, context.clone()).unwrap()),
        })
    }
}

impl MojomEncodable for {{$union.Name}} {
    impl_encodable_for_union!();
    fn compute_size(&self, context: Context) -> usize {
        UNION_SIZE +
        match *self {
{{range $field := $union.Fields}}            {{$union.Name}}::{{$field.Name}}(ref val) => val.compute_size(context.clone()),
{{end}}            {{$union.Name}}::_Unknown(ref val) => 0,
        }
    }
}
{{end}}
`
