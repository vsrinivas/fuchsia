// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const serviceTmpl = `
{{- define "ServiceDeclaration" -}}
{{- $service := . }}
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct {{ $service.Name }}Marker;

#[cfg(target_os = "fuchsia")]
impl fidl::endpoints::UnifiedServiceMarker for {{ $service.Name }}Marker {
    type Proxy = {{ $service.Name }}Proxy;
    type Request = {{ $service.Name }}Request;
    const SERVICE_NAME: &'static str = "{{ $service.ServiceName }}";
}

/// A request for one of the member protocols of {{ $service.Name }}.
///
{{- range $service.DocComments }}
///{{ . }}
{{- end }}
#[cfg(target_os = "fuchsia")]
pub enum {{ $service.Name }}Request {
    {{- range $member := $service.Members }}
    {{- range $service.DocComments }}
    ///{{ . }}
    {{- end }}
    {{ $member.CamelName }}({{ $member.ProtocolType }}RequestStream),
    {{- end}}
}

#[cfg(target_os = "fuchsia")]
impl fidl::endpoints::UnifiedServiceRequest for {{ $service.Name }}Request {
    type Service = {{ $service.Name }}Marker;

    fn dispatch(name: &str, channel: ::fuchsia_async::Channel) -> Self {
        match name {
            {{- range $member := $service.Members }}
            "{{ $member.Name }}" => Self::{{ $member.CamelName }}(
                <{{ $member.ProtocolType }}RequestStream as fidl::endpoints::RequestStream>::from_channel(channel),
            ),
            {{- end }}
            _ => panic!("no such member protocol name for service {{ $service.Name }}"),
        }
    }

    fn member_names() -> &'static [&'static str] {
        &[
        {{- range $member := $service.Members }}
            "{{ $member.Name }}",
        {{- end }}
        ]
    }
}

{{- range $service.DocComments }}
///{{ . }}
{{- end }}
#[cfg(target_os = "fuchsia")]
pub struct {{ $service.Name }}Proxy(Box<dyn fidl::endpoints::MemberOpener>);

#[cfg(target_os = "fuchsia")]
impl fidl::endpoints::UnifiedServiceProxy for {{ $service.Name }}Proxy {
    type Service = {{ $service.Name }}Marker;

    fn from_member_opener(opener: Box<dyn fidl::endpoints::MemberOpener>) -> Self {
        Self(opener)
    }
}

#[cfg(target_os = "fuchsia")]
impl {{ $service.Name }}Proxy {
    {{- range $member := $service.Members }}
    {{- range $member.DocComments }}
    ///{{ . }}
    {{- end }}
    pub fn {{ $member.SnakeName }}(&self) -> Result<{{ $member.ProtocolType }}Proxy, fidl::Error> {
        let (proxy, server) = zx::Channel::create().map_err(fidl::Error::ChannelPairCreate)?;
        self.0.open_member("{{ $member.Name }}", server)?;
        let proxy = ::fuchsia_async::Channel::from_channel(proxy).map_err(fidl::Error::AsyncChannel)?;
        Ok({{ $member.ProtocolType }}Proxy::new(proxy))
    }
    {{- end }}
}
{{- end }}
`
