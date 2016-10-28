// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateEndpoint = `
{{- /* . (dot) refers to a string which is the endpoint's name */ -}}
{{- define "GenerateEndpoint" -}}
{{- $endpoint := .Name -}}
{{- $interface := .Interface -}}

pub struct {{$endpoint}} {
    pipe: message_pipe::MessageEndpoint,
    version: u32,
}

impl {{$endpoint}} {
    pub fn new(pipe: message_pipe::MessageEndpoint) -> {{$endpoint}} {
        {{$endpoint}} {
            pipe: pipe,
            version: {{$interface}}::VERSION,
        }
    }
    pub fn with_version(pipe: message_pipe::MessageEndpoint, version: u32) -> {{$endpoint}} {
        {{$endpoint}} {
            pipe: pipe,
            version: version,
        }
    }
}

impl MojomInterface for {{$endpoint}} {
    fn service_name() -> &'static str { {{$interface}}::SERVICE_NAME }
    fn version(&self) -> u32 { self.version }
    fn pipe(&self) -> &message_pipe::MessageEndpoint { &self.pipe }
    fn unwrap(self) -> message_pipe::MessageEndpoint { self.pipe }
}

impl CastHandle for {{$endpoint}} {
    unsafe fn from_untyped(handle: system::UntypedHandle) -> {{$endpoint}} {
        {{$endpoint}} {
            pipe: message_pipe::MessageEndpoint::from_untyped(handle),
            version: 0, // Since we have no other information, assume its the base
        }
    }
    fn as_untyped(self) -> system::UntypedHandle {
        self.pipe.as_untyped()
    }
}

impl MojomEncodable for {{$endpoint}} {
    impl_encodable_for_interface!();
}

{{- end -}}
`

const GenerateInterface = `
{{- /* . (dot) refers to the Go type |rustgen.InterfaceTemplate| */ -}}
{{- define "GenerateInterface" -}}
{{- $interface := . -}}
// --- {{$interface.Name}} ---

pub mod {{$interface.Name}} {
    pub const SERVICE_NAME: &'static str = "{{$interface.ServiceName}}";
    pub const VERSION: u32 = {{$interface.Version}};
}

{{$client := $interface.Client -}}
{{template "GenerateEndpoint" $client}}

impl<R: {{$interface.Name}}Request> MojomInterfaceSend<R> for {{$client.Name}} {}
impl MojomInterfaceRecv for {{$client.Name}} {
    type Container = {{$interface.Name}}ResponseOption;
}

{{$server := $interface.Server -}}
{{template "GenerateEndpoint" $server}}

impl<R: {{$interface.Name}}Response> MojomInterfaceSend<R> for {{$server.Name}} {}
impl MojomInterfaceRecv for {{$server.Name}} {
    type Container = {{$interface.Name}}RequestOption;
}

// Enums
{{range $enum := $interface.Enums -}}
{{template "GenerateEnum" $enum}}
{{end}}

// Constants
{{range $const := $interface.Constants -}}
pub const {{$const.Name}}: {{$const.Type}} = {{$const.Value}};
{{end}}

pub trait {{$interface.Name}}Request: MojomMessage {}
pub trait {{$interface.Name}}Response: MojomMessage {}

{{- $req_option := printf "%s%s" $interface.Name "RequestOption" }}
{{- $rsp_option := printf "%s%s" $interface.Name "ResponseOption" }}

pub enum {{$req_option}} {
{{range $message := $interface.Messages}}    {{$message.Name}}({{$message.RequestStruct.Name}}),
{{end}}
}

impl MojomMessageOption for {{$req_option}} {
    fn decode_payload(header: MessageHeader, buffer: &[u8], handles: Vec<UntypedHandle>) -> Result<Self, ValidationError> {
         match header.name {
{{range $message := $interface.Messages}}             {{$message.Name}}::ORDINAL => {
                if header.flags != {{- if eq $message.ResponseStruct.Name "" -}}
		           message::MESSAGE_HEADER_NO_FLAG
			   {{- else -}}
                           message::MESSAGE_HEADER_EXPECT_RESPONSE
			   {{- end -}} {
                    return Err(ValidationError::MessageHeaderInvalidFlags);
                }
                match {{$message.RequestStruct.Name}}::deserialize(buffer, handles) {
                    Ok(value) => Ok({{$req_option}}::{{$message.Name}}(value)),
                    Err(err) => return Err(err),
                }
            },
{{end}}             _ => Err(ValidationError::MessageHeaderUnknownMethod),
        }
    }
}

pub enum {{$rsp_option}} {
{{range $message := $interface.Messages}}
{{- if ne $message.ResponseStruct.Name ""}}    {{$message.Name}}({{$message.ResponseStruct.Name}}),
{{end -}}
{{end}}
}

impl MojomMessageOption for {{$rsp_option}} {
    fn decode_payload(header: MessageHeader, buffer: &[u8], handles: Vec<UntypedHandle>) -> Result<Self, ValidationError> {
        if header.flags != message::MESSAGE_HEADER_IS_RESPONSE {
            return Err(ValidationError::MessageHeaderInvalidFlags);
        }
        match header.name {
{{range $message := $interface.Messages}}
{{- if ne $message.ResponseStruct.Name ""}}             {{$message.Name}}::ORDINAL => {
                match {{$message.ResponseStruct.Name}}::deserialize(buffer, handles) {
                    Ok(value) => Ok({{$rsp_option}}::{{$message.Name}}(value)),
                    Err(err) => return Err(err),
                }
            },
{{end -}}
{{end}}             _ => Err(ValidationError::MessageHeaderUnknownMethod),
        }
    }
}

{{range $message := $interface.Messages -}}
/// Message: {{$message.Name}}
pub mod {{$message.Name}} {
    pub const ORDINAL: u32 = {{$message.MessageOrdinal}};
    pub const MIN_VERSION: u32 = {{$message.MinVersion}};
}
{{template "GenerateStruct" $message.RequestStruct}}
impl MojomMessage for {{$message.RequestStruct.Name}} {
    fn min_version() -> u32 { {{$message.Name}}::MIN_VERSION }
    fn create_header() -> MessageHeader {
        MessageHeader::new(
{{- if eq $message.ResponseStruct.Name "" -}}
                           0,
	                   {{$message.Name}}::ORDINAL,
                           message::MESSAGE_HEADER_NO_FLAG)
{{else}}
                           1,
	                   {{$message.Name}}::ORDINAL,
                           message::MESSAGE_HEADER_EXPECT_RESPONSE)
{{end}}
    }
}
impl {{$interface.Name}}Request for {{$message.RequestStruct.Name}} {}

{{if ne $message.ResponseStruct.Name "" -}}
{{template "GenerateStruct" $message.ResponseStruct}}

impl MojomMessage for {{$message.ResponseStruct.Name}} {
    fn min_version() -> u32 { {{$message.Name}}::MIN_VERSION }
    fn create_header() -> MessageHeader {
        MessageHeader::new(1,
	                   {{$message.Name}}::ORDINAL,
			   message::MESSAGE_HEADER_IS_RESPONSE)
    }
}
impl {{$interface.Name}}Response for {{$message.ResponseStruct.Name}} {}

{{- end}}
{{- end -}}
{{- end -}}
`
