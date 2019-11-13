// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "OrdinalMatchPattern" -}}
  {{- range $index, $ordinal := . -}}
    {{- if $index }} | {{ end -}}{{ $ordinal.Ordinal }}
  {{- end -}}
{{ end }}

{{- define "InterfaceDeclaration" -}}
{{- $interface := . }}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct {{ $interface.Name }}Marker;

impl fidl::endpoints::ServiceMarker for {{ $interface.Name }}Marker {
	type Proxy = {{ $interface.Name }}Proxy;
	type RequestStream = {{ $interface.Name }}RequestStream;
{{- if .ServiceName }}
	const DEBUG_NAME: &'static str = "{{ $interface.ServiceName }}";
{{- else }}
	const DEBUG_NAME: &'static str = "(anonymous) {{ $interface.Name }}";
{{- end }}
}

{{- if $interface.ServiceName }}
impl fidl::endpoints::DiscoverableService for {{ $interface.Name }}Marker {}
{{- end }}

pub trait {{ $interface.Name }}ProxyInterface: Send + Sync {
	{{- range $method := $interface.Methods }}
	{{- if $method.HasResponse }}
	type {{ $method.CamelName }}ResponseFut: std::future::Future<Output = Result<(
		{{- range $index, $response := $method.Response -}}
		{{- if (eq $index 0) -}} {{ $response.Type }}
		{{- else -}}, {{ $response.Type }} {{- end -}}
		{{- end -}}
	), fidl::Error>> + Send;
	{{- end -}}

	{{- if $method.HasRequest }}
	fn {{ $method.Name }}(&self,
		{{- range $request := $method.Request }}
		{{ $request.Name }}: {{ $request.BorrowedType }},
		{{- end }}
	)
	{{- if $method.HasResponse -}}
	-> Self::{{ $method.CamelName }}ResponseFut;
	{{- else -}}
	-> Result<(), fidl::Error>;
	{{- end -}}
	{{- end -}}
	{{- end }}
}

#[derive(Debug)]
#[cfg(target_os = "fuchsia")]
pub struct {{ $interface.Name }}SynchronousProxy {
	client: fidl::client::sync::Client,
}

#[cfg(target_os = "fuchsia")]
impl {{ $interface.Name }}SynchronousProxy {
	pub fn new(channel: ::fidl::Channel) -> Self {
		Self { client: fidl::client::sync::Client::new(channel) }
	}

	pub fn into_channel(self) -> ::fidl::Channel {
		self.client.into_channel()
	}

	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	{{- range .DocComments}}
	///{{ . }}
	{{- end}}
	pub fn {{ $method.Name }}(&mut self,
		{{- range $request := $method.Request }}
		mut {{ $request.Name }}: {{ $request.BorrowedType }},
		{{- end }}
		{{- if $method.HasResponse -}}
		___deadline: zx::Time,
		{{- end -}}
	) -> Result<(
		{{- range $index, $response := $method.Response -}}
		{{- if (eq $index 0) -}} {{ $response.Type }}
		{{- else -}}, {{ $response.Type }} {{- end -}}
		{{- end -}}
	), fidl::Error> {
		{{- if $method.HasResponse -}}
			self.client.send_query(&mut (
				{{- range $index, $request := $method.Request -}}
				{{- if (eq $index 0) -}} {{ $request.Name }}
				{{- else -}}, {{ $request.Name }} {{- end -}}
				{{- end -}}
				),
				{{ $method.Ordinals.Write.Ordinal }},
				___deadline,
			)
		{{- else -}}
			self.client.send(&mut (
				{{- range $index, $request := $method.Request -}}
				{{- if (eq $index 0) -}} {{ $request.Name }}
				{{- else -}}, {{ $request.Name }} {{- end -}}
				{{- end -}}
				),
				{{ $method.Ordinals.Write.Ordinal }},
			)
		{{- end -}}
	}
	{{- end -}}
	{{- end }}
}

#[derive(Debug, Clone)]
pub struct {{ $interface.Name }}Proxy {
	client: fidl::client::Client,
}

impl fidl::endpoints::Proxy for {{ $interface.Name }}Proxy {
	type Service = {{ $interface.Name }}Marker;
	fn from_channel(inner: ::fidl::AsyncChannel) -> Self {
		Self::new(inner)
	}
}

impl ::std::ops::Deref for {{ $interface.Name }}Proxy {
	type Target = fidl::client::Client;

	fn deref(&self) -> &Self::Target {
		&self.client
	}
}

impl {{ $interface.Name }}Proxy {
	/// Create a new Proxy for {{ $interface.Name }}
	pub fn new(channel: ::fidl::AsyncChannel) -> Self {
		Self { client: fidl::client::Client::new(channel) }
	}

	/// Attempt to convert the Proxy back into a channel.
	///
	/// This will only succeed if there are no active clones of this Proxy
	/// and no currently-alive EventStream or response futures that came from
	/// this Proxy.
	pub fn into_channel(self) -> Result<::fidl::AsyncChannel, Self> {
		self.client.into_channel().map_err(|client| Self { client })
	}

	/// Get a Stream of events from the remote end of the {{ $interface.Name }} interface
	pub fn take_event_stream(&self) -> {{ $interface.Name }}EventStream {
		{{ $interface.Name }}EventStream {
			event_receiver: self.client.take_event_receiver(),
		}
	}

	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	{{- range .DocComments}}
	///{{ . }}
	{{- end}}
	pub fn {{ $method.Name }}(&self,
		{{- range $request := $method.Request }}
		mut {{ $request.Name }}: {{ $request.BorrowedType }},
		{{- end }}
	)
	{{- if $method.HasResponse -}}
	-> fidl::client::QueryResponseFut<(
		{{- range $index, $response := $method.Response -}}
		{{- if (eq $index 0) -}} {{ $response.Type }}
		{{- else -}}, {{ $response.Type }} {{- end -}}
		{{- end -}}
	)> {
	{{- else -}}
	-> Result<(), fidl::Error> {
	{{- end }}
		{{ $interface.Name }}ProxyInterface::{{ $method.Name }}(self,
		{{- range $request := $method.Request }}
		{{ $request.Name }},
		{{- end }}
		)
	}
	{{- end -}}
	{{- end }}
}

impl {{ $interface.Name}}ProxyInterface for {{ $interface.Name}}Proxy {
	{{- range $method := $interface.Methods }}
	{{- if $method.HasResponse }}
	type {{ $method.CamelName }}ResponseFut = fidl::client::QueryResponseFut<(
		{{- range $index, $response := $method.Response -}}
		{{- if (eq $index 0) -}} {{ $response.Type }}
		{{- else -}}, {{ $response.Type }} {{- end -}}
		{{- end -}}
	)>;
	{{- end -}}

	{{- if $method.HasRequest }}
	fn {{ $method.Name }}(&self,
		{{- range $request := $method.Request }}
		mut {{ $request.Name }}: {{ $request.BorrowedType }},
		{{- end }}
	)
	{{- if $method.HasResponse -}}
	-> Self::{{ $method.CamelName}}ResponseFut {
		self.client.send_query(&mut (
	{{- else -}}
	-> Result<(), fidl::Error> {
		self.client.send(&mut (
	{{- end -}}
		{{- range $index, $request := $method.Request -}}
		{{- if (eq $index 0) -}} {{ $request.Name }}
		{{- else -}}, {{ $request.Name }} {{- end -}}
		{{- end -}}
	), {{ $method.Ordinals.Write.Ordinal }})
	}
	{{- end -}}
	{{- end -}}
}

pub struct {{ $interface.Name }}EventStream {
	event_receiver: fidl::client::EventReceiver,
}

impl ::std::marker::Unpin for {{ $interface.Name }}EventStream {}

impl futures::stream::FusedStream for {{ $interface.Name }}EventStream {
	fn is_terminated(&self) -> bool {
		self.event_receiver.is_terminated()
	}
}

impl futures::Stream for {{ $interface.Name }}EventStream {
	type Item = Result<{{ $interface.Name }}Event, fidl::Error>;

	fn poll_next(mut self: ::std::pin::Pin<&mut Self>, cx: &mut std::task::Context<'_>)
		-> std::task::Poll<Option<Self::Item>>
	{
		let mut buf = match futures::ready!(
			futures::stream::StreamExt::poll_next_unpin(&mut self.event_receiver, cx)?
		) {
			Some(buf) => buf,
			None => return std::task::Poll::Ready(None),
		};
		let (bytes, _handles) = buf.split_mut();
		let (tx_header, _body_bytes) = fidl::encoding::decode_transaction_header(bytes)?;

		#[allow(unreachable_patterns)] // GenOrdinal and Ordinal can overlap
		std::task::Poll::Ready(Some(match tx_header.ordinal() {
			{{- range $method := $interface.Methods }}
			{{- if not $method.HasRequest }}
			{{ template "OrdinalMatchPattern" .Ordinals.Reads }} => {
				let mut out_tuple: (
					{{- range $index, $param := $method.Response -}}
					{{- if ne 0 $index -}}, {{- $param.Type -}}
					{{- else -}} {{- $param.Type -}}
					{{- end -}}
					{{- end -}}
				) = fidl::encoding::Decodable::new_empty();
				fidl::encoding::Decoder::decode_into(&tx_header, _body_bytes, _handles, &mut out_tuple)?;
				Ok((
					{{ $interface.Name }}Event::{{ $method.CamelName }} {
						{{- range $index, $param := $method.Response -}}
						{{- if ne 1 (len $method.Response) -}}
							{{- $param.Name -}}: out_tuple.{{- $index -}},
						{{- else -}}
							{{- $param.Name -}}: out_tuple,
						{{- end -}}
						{{- end -}}
					}
				))
			}
			{{- end -}}
			{{- end }}
			_ => Err(fidl::Error::UnknownOrdinal {
				ordinal: tx_header.ordinal(),
				service_name: <{{ $interface.Name }}Marker as fidl::endpoints::ServiceMarker>::DEBUG_NAME,
			})
		}))
	}
}

{{ $interface.EventDerives }}
pub enum {{ $interface.Name }}Event {
	{{ range $method := $interface.Methods }}
	{{ if not $method.HasRequest }}
	{{ $method.CamelName }} {
		{{ range $param := $method.Response }}
		{{ $param.Name }}: {{ $param.Type }},
		{{ end }}
	},
	{{- end -}}
	{{- end -}}
}

impl {{ $interface.Name }}Event {
	{{- range $method := $interface.Methods }}
	{{- if not $method.HasRequest }}
	#[allow(irrefutable_let_patterns)]
	pub fn into_{{ $method.Name }}(self) -> Option<(
		{{- range $index, $param := $method.Response }}
		{{- if (eq $index 0) -}} {{ $param.Type }}
		{{- else -}}, {{ $param.Type }} {{- end -}}
		{{ end }}
	)> {
		if let {{ $interface.Name }}Event::{{ $method.CamelName }} {
			{{- range $param := $method.Response }}
			{{ $param.Name }},
			{{ end }}
		} = self {
			Some((
				{{- range $index, $param := $method.Response -}}
				{{- if (eq $index 0) -}} {{ $param.Name -}}
				{{- else -}}, {{ $param.Name }} {{- end -}}
				{{- end -}}
			))
		} else {
			None
		}
	}
	{{ end }}
	{{- end }}
}

/// A type which can be used to send responses and events into a borrowed channel.
///
/// Note: this should only be used when the channel must be temporarily
/// borrowed. For a typical sending of events, use the send_ methods
/// on the ControlHandle types, which can be acquired through a
/// RequestStream or Responder type.
#[deprecated(note = "Use {{ $interface.Name }}RequestStream / Responder instead")]
pub struct {{ $interface.Name }}ServerSender<'a> {
	// Some protocols don't define events which would render this channel unused.
	#[allow(unused)]
	channel: &'a ::fidl::Channel,
}

impl <'a> {{ $interface.Name }}ServerSender<'a> {
	pub fn new(channel: &'a ::fidl::Channel) -> Self {
		Self { channel }
	}
	{{- range $method := $interface.Methods }}
	{{- if not $method.HasRequest }}
	pub fn send_{{ $method.Name }}(&self
		{{- range $param := $method.Response -}},
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}}
		{{- end -}}
	) -> Result<(), fidl::Error> {
		::fidl::encoding::with_tls_coding_bufs(|bytes, handles| {
			{{ $interface.Name }}Encoder::encode_{{ $method.Name }}_response(
				bytes, handles,
				{{- range $index, $param := $method.Response -}}
					{{ $param.Name -}},
				{{- end -}}
			)?;
			self.channel.write(&*bytes, &mut *handles).map_err(fidl::Error::ServerResponseWrite)?;
			Ok(())
		})
	}
	{{- end -}}

	{{- if and $method.HasRequest $method.HasResponse }}
	pub fn send_{{ $method.Name }}_response(&self,
		txid: fidl::client::Txid
		{{- range $param := $method.Response -}},
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}}
		{{- end -}}
	) -> Result<(), fidl::Error> {
		::fidl::encoding::with_tls_coding_bufs(|bytes, handles| {
			{{ $interface.Name }}Encoder::encode_{{ $method.Name }}_response(
				bytes, handles,
				txid.as_raw_id(),
				{{- range $index, $param := $method.Response -}}
					{{ $param.Name -}},
				{{- end -}}
			)?;
			self.channel.write(&*bytes, &mut *handles).map_err(fidl::Error::ServerResponseWrite)?;
			Ok(())
		})
	}
	{{- end -}}

	{{- end }}
}

/// A Stream of incoming requests for {{ $interface.Name }}
pub struct {{ $interface.Name }}RequestStream {
	inner: ::std::sync::Arc<fidl::ServeInner>,
	is_terminated: bool,
}

impl ::std::marker::Unpin for {{ $interface.Name }}RequestStream {}

impl futures::stream::FusedStream for {{ $interface.Name }}RequestStream {
	fn is_terminated(&self) -> bool {
		self.is_terminated
	}
}

impl fidl::endpoints::RequestStream for {{ $interface.Name }}RequestStream {
	type Service = {{ $interface.Name }}Marker;

	/// Consume a channel to make a {{ $interface.Name }}RequestStream
	fn from_channel(channel: ::fidl::AsyncChannel) -> Self {
		Self {
			inner: ::std::sync::Arc::new(fidl::ServeInner::new(channel)),
			is_terminated: false,
		}
	}

	/// ControlHandle for the remote connection
	type ControlHandle = {{ $interface.Name }}ControlHandle;

	/// ControlHandle for the remote connection
	fn control_handle(&self) -> Self::ControlHandle {
		{{ $interface.Name }}ControlHandle { inner: self.inner.clone() }
	}

	fn into_inner(self) -> (::std::sync::Arc<fidl::ServeInner>, bool) {
		(self.inner, self.is_terminated)
	}

	fn from_inner(inner: ::std::sync::Arc<fidl::ServeInner>, is_terminated: bool)
		-> Self
	{
		Self { inner, is_terminated }
	}
}

impl futures::Stream for {{ $interface.Name }}RequestStream {
	type Item = Result<{{ $interface.Name }}Request, fidl::Error>;

	fn poll_next(mut self: ::std::pin::Pin<&mut Self>, cx: &mut std::task::Context<'_>)
		-> std::task::Poll<Option<Self::Item>>
	{
		let this = &mut *self;
		if this.inner.poll_shutdown(cx) {
			this.is_terminated = true;
			return std::task::Poll::Ready(None);
		}
		if this.is_terminated {
			panic!("polled {{ $interface.Name }}RequestStream after completion");
		}
		::fidl::encoding::with_tls_coding_bufs(|bytes, handles| {
			match this.inner.channel().read(cx, bytes, handles) {
				std::task::Poll::Ready(Ok(())) => {},
				std::task::Poll::Pending => return std::task::Poll::Pending,
				std::task::Poll::Ready(Err(zx_status::Status::PEER_CLOSED)) => {
					this.is_terminated = true;
					return std::task::Poll::Ready(None);
				}
				std::task::Poll::Ready(Err(e)) => return std::task::Poll::Ready(Some(Err(fidl::Error::ServerRequestRead(e)))),
			}

			// A message has been received from the channel
			let (header, _body_bytes) = fidl::encoding::decode_transaction_header(bytes)?;
			if !header.is_compatible() {
				return std::task::Poll::Ready(Some(Err(fidl::Error::IncompatibleMagicNumber(header.magic_number()))));
			}

			#[allow(unreachable_patterns)] // GenOrdinal and Ordinal can overlap
			std::task::Poll::Ready(Some(match header.ordinal() {
				{{- range $method := $interface.Methods }}
				{{- if $method.HasRequest }}
				{{ template "OrdinalMatchPattern" .Ordinals.Reads }} => {
					let mut req: (
						{{- range $index, $param := $method.Request -}}
							{{- if ne 0 $index -}}, {{- $param.Type -}}
							{{- else -}} {{- $param.Type -}}
							{{- end -}}
						{{- end -}}
					) = fidl::encoding::Decodable::new_empty();
					fidl::encoding::Decoder::decode_into(&header, _body_bytes, handles, &mut req)?;
					let control_handle = {{ $interface.Name }}ControlHandle {
						inner: this.inner.clone(),
					};

					Ok({{ $interface.Name }}Request::{{ $method.CamelName }} {
						{{- range $index, $param := $method.Request -}}
							{{- if ne 1 (len $method.Request) -}}
							{{ $param.Name }}: req.{{ $index }},
							{{- else -}}
							{{ $param.Name }}: req,
							{{- end -}}
						{{- end -}}
						{{- if $method.HasResponse -}}
							responder: {{- $interface.Name -}}{{- $method.CamelName -}}Responder {
								control_handle: ::std::mem::ManuallyDrop::new(control_handle),
								tx_id: header.tx_id(),
								ordinal: header.ordinal(),
							},
							{{- else -}}
							control_handle,
						{{- end -}}
					})
				}
				{{- end }}
				{{- end }}
				_ => Err(fidl::Error::UnknownOrdinal {
					ordinal: header.ordinal(),
					service_name: <{{ $interface.Name }}Marker as fidl::endpoints::ServiceMarker>::DEBUG_NAME,
				}),
			}))
		})
	}
}

/// Represents a single request.
/// RequestMessages should only be used for manual deserialization when higher level
/// structs such as RequestStream cannot be used. One usually would only encounter
/// such scenarios when working with legacy FIDL code (prior to FIDL generated client/server bindings).
{{ $interface.RequestDerives }}
#[deprecated(note = "Use {{ $interface.Name }}Request instead")]
pub enum {{ $interface.Name }}RequestMessage {
	{{- range $method := $interface.Methods }}
        {{- if $method.HasRequest }}
	{{- range .DocComments}}
	///{{ . }}
	{{- end}}
	{{ $method.CamelName }} {
		{{ range $index, $param := $method.Request }}
		{{ $param.Name }}: {{ $param.Type }},
		{{ end -}}
		{{- if $method.HasResponse -}}
			tx_id: fidl::client::Txid,
		{{- end -}}
	},
	{{- end }}
	{{- end }}
}

impl {{ $interface.Name }}RequestMessage {
	pub fn decode(bytes: &[u8], _handles: &mut [fidl::Handle]) -> Result<{{ $interface.Name }}RequestMessage, fidl::Error> {
		let (header, _body_bytes) = fidl::encoding::decode_transaction_header(bytes)?;

		#[allow(unreachable_patterns)] // GenOrdinal and Ordinal can overlap
		match header.ordinal() {
			{{- range $method := $interface.Methods }}
			{{- if $method.HasRequest }}
			{{ template "OrdinalMatchPattern" .Ordinals.Reads }} => {
				let mut out_tuple: (
					{{- range $index, $param := $method.Request -}}
						{{- if ne 0 $index -}}, {{- $param.Type -}}
						{{- else -}} {{- $param.Type -}}
						{{- end -}}
					{{- end -}}
				) = fidl::encoding::Decodable::new_empty();
				fidl::encoding::Decoder::decode_into(&header, _body_bytes, _handles, &mut out_tuple)?;

				Ok({{ $interface.Name }}RequestMessage::{{ $method.CamelName }} {
					{{- range $index, $param := $method.Request -}}
						{{- if ne 1 (len $method.Request) -}}
						{{ $param.Name }}: out_tuple.{{ $index }},
						{{- else -}}
						{{ $param.Name }}: out_tuple,
						{{- end -}}
					{{- end -}}
					{{- if $method.HasResponse -}}
						tx_id: header.tx_id().into(),
					{{- end -}}
				})
			}
			{{- end }}
			{{- end }}
			_ => Err(fidl::Error::UnknownOrdinal {
				ordinal: header.ordinal(),
				service_name: <{{ $interface.Name }}Marker as fidl::endpoints::ServiceMarker>::DEBUG_NAME,
			}),
		}
	}


}

{{- range .DocComments}}
///{{ . }}
{{- end}}
{{ $interface.RequestDerives }}
pub enum {{ $interface.Name }}Request {
	{{- range $method := $interface.Methods }}
        {{- if $method.HasRequest }}
	{{- range .DocComments}}
	///{{ . }}
	{{- end}}
	{{ $method.CamelName }} {
		{{ range $index, $param := $method.Request }}
		{{ $param.Name }}: {{ $param.Type }},
		{{ end -}}
		{{- if $method.HasResponse -}}
		responder: {{ $interface.Name }}{{ $method.CamelName }}Responder,
		{{- else -}}
		control_handle: {{ $interface.Name }}ControlHandle,
		{{- end -}}
	},
	{{- end }}
	{{- end }}
}

impl {{ $interface.Name }}Request {
	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	#[allow(irrefutable_let_patterns)]
	pub fn into_{{ $method.Name }}(self) -> Option<(
		{{- range $param := $method.Request }}
		{{ $param.Type }},
		{{ end }}
		{{- if $method.HasResponse -}}
		{{ $interface.Name }}{{ $method.CamelName }}Responder
		{{- else -}}
		{{ $interface.Name }}ControlHandle
		{{- end }}
	)> {
		if let {{ $interface.Name }}Request::{{ $method.CamelName }} {
			{{- range $param := $method.Request }}
			{{ $param.Name }},
			{{ end }}
			{{- if $method.HasResponse -}}
			responder,
			{{- else -}}
			control_handle,
			{{- end }}
		} = self {
			Some((
				{{- range $param := $method.Request -}}
				{{- $param.Name -}},
				{{- end -}}
				{{- if $method.HasResponse -}}
				responder
				{{- else -}}
				control_handle
				{{- end -}}
			))
		} else {
			None
		}
	}
	{{ end }}
	{{- end }}

        /// Name of the method defined in FIDL
        pub fn method_name(&self) -> &'static str {
          match *self {
            {{- range $method := $interface.Methods }}
              {{- if $method.HasRequest }}
                {{ $interface.Name }}Request::{{ $method.CamelName }}{..} => "{{ $method.Name }}",
              {{- end }}
            {{- end }}
          }
        }
}

pub struct {{ $interface.Name }}Encoder;

impl {{ $interface.Name }}Encoder {
	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	pub fn encode_{{ $method.Name }}_request<'a>(
		out_bytes: &'a mut Vec<u8>,
		out_handles: &'a mut Vec<fidl::Handle>,
		{{- if $method.HasResponse }}
		tx_id: u32,
		{{- end -}}
		{{- range $index, $param := $method.Request -}}
		mut in_{{ $param.Name -}}: {{ $param.BorrowedType -}},
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let header = fidl::encoding::TransactionHeader::new(
			{{- if $method.HasResponse }}
			tx_id,
			{{- else -}}
			0,
			{{- end -}}
			{{ $method.Ordinals.Write.Ordinal }});
		let mut body = (
			{{- range $index, $param := $method.Request -}}
			in_{{ $param.Name -}},
			{{- end -}}
		);
		let mut msg = fidl::encoding::TransactionMessage { header, body: &mut body };
		fidl::encoding::Encoder::encode(out_bytes, out_handles, &mut msg)?;
		Ok(())
	}
	{{- end }}
	{{- if $method.HasResponse }}
	pub fn encode_{{ $method.Name }}_response<'a>(
		out_bytes: &'a mut Vec<u8>,
		out_handles: &'a mut Vec<fidl::Handle>,
		{{- if $method.HasRequest }}
		tx_id: u32,
		{{- end -}}
		{{- range $param := $method.Response -}}
		mut in_{{ $param.Name -}}: {{ $param.BorrowedType -}},
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let header = fidl::encoding::TransactionHeader::new(
			{{- if $method.HasRequest }}
			tx_id,
			{{- else -}}
			0,
			{{- end -}}
			{{ $method.Ordinals.Write.Ordinal }});
		let mut body = (
			{{- range $index, $param := $method.Response -}}
			in_{{ $param.Name -}},
			{{- end -}}
		);
		let mut msg = fidl::encoding::TransactionMessage { header, body: &mut body };
		fidl::encoding::Encoder::encode(out_bytes, out_handles, &mut msg)?;
		Ok(())
	}
	{{- end }}
	{{- end -}}
}

#[derive(Debug, Clone)]
pub struct {{ $interface.Name }}ControlHandle {
	inner: ::std::sync::Arc<fidl::ServeInner>,
}

impl ::std::ops::Deref for {{ $interface.Name }}ControlHandle {
	type Target = ::std::sync::Arc<fidl::ServeInner>;

	fn deref(&self) -> &Self::Target {
		&self.inner
	}
}

impl {{ $interface.Name }}ControlHandle {
	pub fn shutdown(&self) {
		self.inner.shutdown()
	}

	pub fn shutdown_with_epitaph(&self, status: zx_status::Status) {
		self.inner.shutdown_with_epitaph(status)
	}

	{{- range $method := $interface.Methods }}
	{{- if not $method.HasRequest }}
	pub fn send_{{ $method.Name }}(&self
		{{- range $param := $method.Response -}},
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}}
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let header = fidl::encoding::TransactionHeader::new(
			0, {{ $method.Ordinals.Write.Ordinal }});

		let mut response = (
			{{- range $index, $param := $method.Response -}}
				{{- if ne 0 $index -}}, {{ $param.Name -}}
				{{- else -}} {{ $param.Name -}}
				{{- end -}}
			{{- end -}}
		);

		let mut msg = fidl::encoding::TransactionMessage {
			header,
			body: &mut response,
		};

		::fidl::encoding::with_tls_encoded(&mut msg, |bytes, handles| {
			self.inner.channel().write(&*bytes, &mut *handles).map_err(fidl::Error::ServerResponseWrite)
		})?;

		Ok(())
	}
	{{ end -}}
	{{- end -}}

}

/* beginning of response types */
{{- range $method := $interface.Methods }}
{{- if and $method.HasRequest $method.HasResponse }}
#[must_use = "FIDL methods require a response to be sent"]
#[derive(Debug)]
pub struct {{ $interface.Name }}{{ $method.CamelName }}Responder {
	control_handle: ::std::mem::ManuallyDrop<{{ $interface.Name }}ControlHandle>,
	tx_id: u32,
	ordinal: u64,
}

impl ::std::ops::Drop for {{ $interface.Name }}{{ $method.CamelName }}Responder {
	fn drop(&mut self) {
		// Shutdown the channel if the responder is dropped without sending a response
		// so that the client doesn't hang. To prevent this behavior, some methods
		// call "drop_without_shutdown"
		self.control_handle.shutdown();
		// Safety: drops once, never accessed again
		unsafe { ::std::mem::ManuallyDrop::drop(&mut self.control_handle) };
	}
}

impl {{ $interface.Name }}{{ $method.CamelName }}Responder {
	pub fn control_handle(&self) -> &{{ $interface.Name }}ControlHandle {
		&self.control_handle
	}

	/// Drop the Responder without setting the channel to shutdown.
	///
	/// This method shouldn't normally be used-- instead, send a response
	/// to prevent the channel from shutting down.
	pub fn drop_without_shutdown(mut self) {
		// Safety: drops once, never accessed again due to mem::forget
		unsafe { ::std::mem::ManuallyDrop::drop(&mut self.control_handle) };
		// Prevent Drop from running (which would shut down the channel)
		::std::mem::forget(self);
	}

	/// Sends a response to the FIDL transaction.
	///
	/// Sets the channel to shutdown if an error occurs.
	pub fn send(self,
		{{- range $param := $method.Response -}}
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}},
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let r = self.send_raw(
			{{- range $index, $param := $method.Response -}}
			{{ $param.Name -}},
			{{- end -}}
		);
		if r.is_err() {
			self.control_handle.shutdown();
		}
		self.drop_without_shutdown();
		r
	}

	/// Similar to "send" but does not shutdown the channel if
	/// an error occurs.
	pub fn send_no_shutdown_on_err(self,
		{{- range $param := $method.Response -}}
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}},
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let r = self.send_raw(
			{{- range $index, $param := $method.Response -}}
			{{ $param.Name -}},
			{{- end -}}
		);
		self.drop_without_shutdown();
		r
	}

	fn send_raw(&self,
		{{- range $param := $method.Response -}}
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}},
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let header = fidl::encoding::TransactionHeader::new(self.tx_id, self.ordinal);

		let mut response = (
			{{- range $index, $param := $method.Response -}}
			{{- if ne 0 $index -}}, {{ $param.Name -}}
			{{- else -}} {{ $param.Name -}}
			{{- end -}}
			{{- end -}}
		);

		let mut msg = fidl::encoding::TransactionMessage {
			header,
			body: &mut response,
		};

		::fidl::encoding::with_tls_coding_bufs(|bytes, handles| {
			::fidl::encoding::Encoder::encode(bytes, handles, &mut msg)?;
			self.control_handle.inner.channel().write(&*bytes, &mut *handles)
				.map_err(fidl::Error::ServerResponseWrite)?;
			Ok(())
		})
	}
}
{{- end -}}
{{- end -}}
{{- end -}}
`
