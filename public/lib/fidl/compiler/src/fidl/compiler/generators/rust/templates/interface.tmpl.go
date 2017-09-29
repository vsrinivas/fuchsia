// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateEndpoint = `
{{- /* . (dot) refers to a string which is the endpoint's name */ -}}
{{- define "GenerateEndpoint" -}}
{{- $endpoint := .Name -}}
{{- $interface := .Interface -}}

pub struct {{$endpoint}}(::zircon::Channel);

impl ::zircon::HandleRef for {{$endpoint}} {
    #[inline]
    fn as_handle_ref(&self) -> ::zircon::HandleRef {
        self.0.get_ref()
    }
}

impl From<{{$endpoint}}> for ::zircon::Handle {
    #[inline]
    fn from(x: $endpoint) -> ::zircon::Handle {
        x.0.into()
    }
}

impl From<::zircon::Handle> for {{$endpoint}} {
    #[inline]
    fn from(handle: ::zircon::Handle) -> Self {
        {{$endpoint}}(handle.into())
    }
}

impl ::zircon::HandleBased for {{$endpoint}}

impl_codable_handle!({{$endpoint}});

{{- end -}}
`

const GenerateInterface = `
{{- /* . (dot) refers to the Go type |rustgen.InterfaceTemplate| */ -}}
{{- define "GenerateInterface" -}}
{{- $interface := . -}}
// --- {{$interface.Name}} ---

use fidl::{self, DecodeBuf, EncodeBuf, EncodablePtr, DecodablePtr, Stub};
use futures::{Future, future};
use zircon;
use tokio_core::reactor;

pub trait {{$interface.Name}} {
    {{range $message := $interface.Messages}}
        fn {{$message.Name}}(&mut self
            {{- range $index, $field := $message.RequestStruct.Fields -}}
                , {{$field.Name}}: {{$field.Type}}
            {{- end -}}
        )
        {{- if ne $message.ResponseStruct.Name "" }}
            -> fidl::BoxFuture<{{template "GenerateResponseType" $message.ResponseStruct}}>
        {{end}};
    {{end}}

    /// Create a FIDL dispatcher from this service implementation.
    #[inline]
    fn dispatch(self) -> {{$interface.Name}}Dispatcher<Self> where Self: Sized {
        {{$interface.Name}}Dispatcher(self)
    }
}

pub struct {{$interface.Name}}Dispatcher<T: {{$interface.Name}}>(pub T);

impl<T: {{$interface.Name}}> Stub for {{$interface.Name}}Dispatcher<T> {
    type Service = {{$interface.Name}}Service;

    // TODO(cramertj): consider optimizing with an enum to avoid unnecessary boxing
    type DispatchFuture = fidl::BoxFuture<EncodeBuf>;

    #[inline]
    fn dispatch_with_response(&mut self, request: &mut DecodeBuf) -> Self::DispatchFuture {
        let name: u32 = ::fidl::Decodable::decode(request, 0, 8).unwrap();
        match name {
            {{range $message := $interface.Messages}}
                {{if ne $message.ResponseStruct.Name ""}}
                    {{$message.MessageOrdinal}} => self.
                    {{- $message.RawName}}(request),
                {{end}}
            {{end}}
            _ => Box::new(future::err(fidl::Error::UnknownOrdinal))
        }
    }

    #[inline]
    fn dispatch(&mut self, request: &mut ::fidl::DecodeBuf) -> Result<(), fidl::Error> {
        let name: u32 = ::fidl::Decodable::decode(request, 0, 8).unwrap();
        match name {
            {{range $message := $interface.Messages}}
                {{if eq $message.ResponseStruct.Name ""}}
                    {{$message.MessageOrdinal}} => self.{{- $message.RawName}}(request),
                {{end}}
            {{end}}
            _ => Err(::fidl::Error::UnknownOrdinal)
        }
    }
}

impl<T: {{$interface.Name}}> {{$interface.Name}}Dispatcher<T> {
    {{- range $message := $interface.Messages}}
        #[inline]
        fn {{$message.RawName}}(&mut self, request: &mut fidl::DecodeBuf)
            {{- if eq $message.ResponseStruct.Name "" }}
                -> Result<(), fidl::Error>
                {
                    let request: {{$message.RequestStruct.Name}} = try!(::fidl::DecodablePtr::decode_obj(request, 16));
                    self.0.{{$message.Name}}(
                        {{- range $index, $field := $message.RequestStruct.Fields -}}
                        {{- if $index}}, {{end -}}
                        request.{{- $field.Name -}}
                        {{- end -}}
                    );
                    Ok(())
                }
            {{- else}}
                -> Box<Future<Item = EncodeBuf, Error = fidl::Error> + Send>
                {
                    let r: ::fidl::Result<{{$message.RequestStruct.Name}}> = ::fidl::DecodablePtr::decode_obj(request, 24);
                    match r {
                        Ok(request) => Box::new(self.0.{{$message.Name}}(
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
                                {{range $field := $message.ResponseStruct.Fields}}
                                    {{$field.Name}}: {{$field.Name}},
                                {{end}}
                            };
                            let mut encode_buf = ::fidl::EncodeBuf::new_response({{$message.MessageOrdinal}});
                            ::fidl::EncodablePtr::encode_obj(response, &mut encode_buf);
                            encode_buf
                        })),
                        Err(error) => Box::new(future::err(error))
                    }
                }
            {{end}}
    {{end}}
}


pub struct {{$interface.Name}}Proxy(fidl::Client);

impl {{$interface.Name}} for {{$interface.Name}}Proxy {
    {{- range $message := $interface.Messages}}
        #[inline]
        fn {{$message.Name}}(&mut self
            {{- range $field := $message.RequestStruct.Fields}}
                , {{$field.Name}}: {{$field.Type}}
            {{- end -}} )
            {{- if ne $message.ResponseStruct.Name "" }}
                -> fidl::BoxFuture<{{template "GenerateResponseType" $message.ResponseStruct}}>
            {{- end}}
        {
            let request = {{$message.RequestStruct.Name}} {
                {{range $field := $message.RequestStruct.Fields}}
                    {{$field.Name}}: {{$field.Name}},
                {{end}}
            };
            let mut encode_buf =
                ::fidl::EncodeBuf::
                new_request{{if ne $message.ResponseStruct.Name "" -}}_expecting_response{{- end}}
                ({{$message.MessageOrdinal}});

            // TODO(cramertj) handle the failure case of the send_msg call below

            ::fidl::EncodablePtr::encode_obj(request, &mut encode_buf);
            {{if eq $message.ResponseStruct.Name ""}}
                self.0.send_msg(&mut encode_buf);
            {{else}}
                Box::new(self.0.send_msg_expect_response(&mut encode_buf).and_then(|mut decode_buf| {
                    let r: {{$message.ResponseStruct.Name}} = try!(::fidl::DecodablePtr::decode_obj(&mut decode_buf, 24));
                    Ok(
                        {{- if ne 1 (len $message.ResponseStruct.Fields) -}} ( {{- end -}}
                        {{- range $index, $field := $message.ResponseStruct.Fields -}}
                            {{- if $index}},{{end -}}
                            r.{{- $field.Name -}}
                        {{- end -}}
                        {{- if ne 1 (len $message.ResponseStruct.Fields) -}}
                    ) {{- end -}}
                )
                }))
            {{end}}
        }
    {{end}}
}

pub struct {{$interface.Name}}Service;
impl fidl::FidlService for {{$interface.Name}}Service {
    type Proxy = {{$interface.Name}}Proxy;

    #[inline]
    fn new_proxy(client_end: fidl::ClientEnd<Self>, handle: &reactor::Handle) -> Result<Self::Proxy, fidl::Error> {
        let channel = ::tokio_fuchsia::Channel::from_channel(client_end.into_channel(), handle)?;
        Ok({{$interface.Name}}Proxy(fidl::Client::new(channel, handle)))
    }

    #[inline]
    fn new_pair(handle: &reactor::Handle) -> Result<(Self::Proxy, fidl::ServerEnd<Self>), fidl::Error> {
        let (s1, s2) = zircon::Channel::create(zircon::ChannelOpts::Normal).unwrap();
        let client_end = fidl::ClientEnd::new(s1);
        let server_end = fidl::ServerEnd::new(s2);
        Ok((Self::new_proxy(client_end, handle)?, server_end))
    }

    const NAME: &'static str = "{{$interface.ServiceName}}";
    const VERSION: u32 = {{$interface.Version}};
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
