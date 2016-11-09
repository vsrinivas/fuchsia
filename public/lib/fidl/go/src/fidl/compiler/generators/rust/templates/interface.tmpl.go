// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateEndpoint = `
{{- /* . (dot) refers to a string which is the endpoint's name */ -}}
{{- define "GenerateEndpoint" -}}
{{- $endpoint := .Name -}}
{{- $interface := .Interface -}}

pub struct {{$endpoint}}(::magenta::Channel);

impl ::magenta::HandleBase for {{$endpoint}} {
    fn get_ref(&self) -> ::magenta::HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: ::magenta::Handle) -> Self {
        {{$endpoint}}(::magenta::Channel::from_handle(handle))
    }
}

impl_codable_handle!({{$endpoint}});

{{- end -}}
`

const GenerateInterface = `
{{- /* . (dot) refers to the Go type |rustgen.InterfaceTemplate| */ -}}
{{- define "GenerateInterface" -}}
{{- $interface := . -}}
// --- {{$interface.Name}} ---

pub trait {{$interface.Name}} {
{{range $message := $interface.Messages}}    fn {{$message.Name}}(&mut self
{{- range $index, $field := $message.RequestStruct.Fields -}}
    , {{$field.Name}}: {{$field.Type}}
{{- end -}}
)
    {{- if ne $message.ResponseStruct.Name "" }} -> ::fidl::Future<{{template "GenerateResponseType" $message.ResponseStruct}}, ::fidl::Error>{{end}};
{{end -}}
}

pub trait {{$interface.Name}}_Stub : {{$interface.Name}} {
    fn dispatch_with_response_Impl(&mut self, request: &mut ::fidl::DecodeBuf) -> ::fidl::Future<::fidl::EncodeBuf, ::fidl::Error> {
        let name: u32 = ::fidl::Decodable::decode(request, 0, 8).unwrap();
        match name {
{{range $message := $interface.Messages}}{{if ne $message.ResponseStruct.Name ""}}            {{$message.MessageOrdinal}} => self.
    {{- $message.RawName}}_Raw(request),
{{end}}{{end}}            _ => ::fidl::Future::failed(::fidl::Error::UnknownOrdinal)
        }
    }

    fn dispatch_Impl(&mut self, request: &mut ::fidl::DecodeBuf) -> Result<(), ::fidl::Error> {
        let name: u32 = ::fidl::Decodable::decode(request, 0, 8).unwrap();
        match name {
{{range $message := $interface.Messages}}{{if eq $message.ResponseStruct.Name ""}}            {{$message.MessageOrdinal}} => self.
    {{- $message.RawName}}_Raw(request),
{{end}}{{end}}            _ => Err(::fidl::Error::UnknownOrdinal)
        }
    }
{{range $message := $interface.Messages}}
    fn {{$message.RawName}}_Raw(&mut self, request: &mut ::fidl::DecodeBuf)
        {{- if eq $message.ResponseStruct.Name "" }} -> Result<(), ::fidl::Error> {
        let request: {{$message.RequestStruct.Name}} = try!(::fidl::DecodablePtr::decode_obj(request, 16));
        self.{{$message.Name}}(
{{- range $index, $field := $message.RequestStruct.Fields -}}
    {{- if $index}}, {{end -}}
    request.{{- $field.Name -}}
{{- end -}}
            );
        Ok(())
    }
    {{- else}} -> ::fidl::Future<::fidl::EncodeBuf, ::fidl::Error> {
        let r: ::fidl::Result<{{$message.RequestStruct.Name}}> = ::fidl::DecodablePtr::decode_obj(request, 24);
        match r {
            Ok(request) => self.{{$message.Name}}(
{{- range $index, $field := $message.RequestStruct.Fields -}}
    {{- if $index}}, {{end -}}
    request.{{- $field.Name -}}
{{- end -}}
            ).map(|
{{- if ne 1 (len $message.ResponseStruct.Fields) -}} ( {{- end -}}
{{- range $index, $field := $message.ResponseStruct.Fields -}}
    {{- if $index}}, {{end -}}
    {{- $field.Name -}}
{{- end -}}
{{- if ne 1 (len $message.ResponseStruct.Fields) -}} ) {{- end -}}
                | {
                let response = {{$message.ResponseStruct.Name}} {
{{range $field := $message.ResponseStruct.Fields}}                    {{$field.Name}}: {{$field.Name}},
{{end}}                };
                let mut encode_buf = ::fidl::EncodeBuf::new_response({{$message.MessageOrdinal}});
                ::fidl::EncodablePtr::encode_obj(response, &mut encode_buf);
                encode_buf
                }),
            Err(error) => ::fidl::Future::failed(error)
        }
    }{{end}}
{{end -}}
}

pub struct {{$interface.Name}}_Proxy(::fidl::Client);

impl {{$interface.Name}} for {{$interface.Name}}_Proxy {
{{- range $message := $interface.Messages}}
    fn {{$message.Name}}(&mut self
{{- range $field := $message.RequestStruct.Fields}}, {{$field.Name}}: {{$field.Type}}
{{- end -}} )
{{- if ne $message.ResponseStruct.Name "" }} -> ::fidl::Future<{{template "GenerateResponseType" $message.ResponseStruct}}, ::fidl::Error>
{{- end}} {
        let request = {{$message.RequestStruct.Name}} {
{{range $field := $message.RequestStruct.Fields}}            {{$field.Name}}: {{$field.Name}},
{{end}}        };
        let mut encode_buf = ::fidl::EncodeBuf::new_request{{if ne $message.ResponseStruct.Name "" -}}
            _expecting_response
        {{- end}}({{$message.MessageOrdinal}});
        ::fidl::EncodablePtr::encode_obj(request, &mut encode_buf);
{{if eq $message.ResponseStruct.Name ""}}        self.0.send_msg(&mut encode_buf);
{{else}}        self.0.send_msg_expect_response(&mut encode_buf).and_then(|mut decode_buf| {
            let r: {{$message.ResponseStruct.Name}} = try!(::fidl::DecodablePtr::decode_obj(&mut decode_buf, 24));
            Ok(
{{- if ne 1 (len $message.ResponseStruct.Fields) -}} ( {{- end -}}
{{- range $index, $field := $message.ResponseStruct.Fields -}}
{{- if $index}}, {{end -}}
r.{{- $field.Name -}}
{{- end -}}
{{- if ne 1 (len $message.ResponseStruct.Fields) -}} ) {{- end -}}
            )
        })
{{end}}    }
{{end -}}
}

pub mod {{$interface.Name}}_Metadata {
    pub const SERVICE_NAME: &'static str = "{{$interface.ServiceName}}";
    pub const VERSION: u32 = {{$interface.Version}};
}

{{$client := $interface.Client -}}
{{template "GenerateEndpoint" $client}}

pub fn {{$interface.Name}}_new_Proxy(client: {{$interface.Client.Name}}) -> {{$interface.Name}}_Proxy {
    let client = ::fidl::Client::new(client.0);
    {{$interface.Name}}_Proxy(client)
}

{{$server := $interface.Server -}}
{{template "GenerateEndpoint" $server}}

impl {{$interface.Server.Name}} {
    pub fn into_interface_ptr(self) -> ::fidl::InterfacePtr<Self> {
        ::fidl::InterfacePtr {
            inner: self,
            version: {{$interface.Name}}_Metadata::VERSION,
        }
    }
}

// Enums
{{range $enum := $interface.Enums -}}
{{template "GenerateEnum" $enum}}
{{end}}

// Constants
{{range $const := $interface.Constants -}}
pub const {{$const.Name}}: {{$const.Type}} = {{$const.Value}};
{{end}}

{{range $message := $interface.Messages -}}
/// Message: {{$message.Name}}
{{template "GenerateStruct" $message.RequestStruct}}

{{if ne $message.ResponseStruct.Name "" -}}
{{template "GenerateStruct" $message.ResponseStruct}}

{{end -}}
{{- end -}}
{{- end -}}

{{- /* . (dot) refers to the Go type |rustgen.StructTemplate| */ -}}
{{- define "GenerateResponseType" -}}
{{- $struct := . -}}
{{- if ne 1 (len $struct.Fields) -}} ( {{- end -}}
{{- range $index, $field := $struct.Fields -}}
{{- if $index}}, {{end -}}
{{- $field.Type -}}
{{- end -}}
{{- if ne 1 (len $struct.Fields) -}} ) {{- end -}}
{{- end -}}
`
