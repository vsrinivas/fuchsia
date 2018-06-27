// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "GenerateImplGenerics" -}}
{{- $interface := . -}}
State,
OnOpen: FnMut(&mut State, {{ $interface.Name }}ControlHandle) -> OnOpenFut,
OnOpenFut: Future<Item = (), Error = Never> + Send,
{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	{{ $method.CamelName }}: FnMut(&mut State,
	{{- range $request := $method.Request -}} {{ $request.Type -}},{{- end -}}
	{{- if $method.HasResponse -}}
	{{- $interface.Name -}}{{- $method.CamelName -}}Responder
	{{- else -}}
	{{ $interface.Name -}}ControlHandle
	{{- end }}) -> {{ $method.CamelName }}Fut,
	{{ $method.CamelName }}Fut: Future<Item = (), Error = Never> + Send,
	{{- end }}
{{- end -}}
{{- end -}}

{{- define "InterfaceDeclaration" -}}
{{- $interface := . }}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct {{ $interface.Name }}Marker;

impl fidl::endpoints2::ServiceMarker for {{ $interface.Name }}Marker {
	type Proxy = {{ $interface.Name }}Proxy;
	type RequestStream = {{ $interface.Name }}RequestStream;
	const NAME: &'static str = "{{ $interface.ServiceName }}";
}

pub trait {{ $interface.Name }}ProxyInterface: Send + Sync {
	{{- range $method := $interface.Methods }}
	{{- if $method.HasResponse }}
	type {{ $method.CamelName }}ResponseFut: Future<Item = (
		{{- range $index, $response := $method.Response -}}
		{{- if (eq $index 0) -}} {{ $response.Type }}
		{{- else -}}, {{ $response.Type }} {{- end -}}
		{{- end -}}
	), Error = fidl::Error> + Send;
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

#[derive(Debug, Clone)]
pub struct {{ $interface.Name }}Proxy {
	client: fidl::client2::Client,
}

impl fidl::endpoints2::Proxy for {{ $interface.Name }}Proxy {
	fn from_channel(inner: async::Channel) -> Self {
		Self::new(inner)
	}
}

impl Deref for {{ $interface.Name }}Proxy {
	type Target = fidl::client2::Client;

	fn deref(&self) -> &Self::Target {
		&self.client
	}
}

impl {{ $interface.Name }}Proxy {
	pub fn new(channel: async::Channel) -> Self {
		Self { client: fidl::client2::Client::new(channel) }
	}

	pub fn take_event_stream(&self) -> {{ $interface.Name }}EventStream {
		{{ $interface.Name }}EventStream {
			event_receiver: self.client.take_event_receiver(),
		}
	}

	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	pub fn {{ $method.Name }}(&self,
		{{- range $request := $method.Request }}
		mut {{ $request.Name }}: {{ $request.BorrowedType }},
		{{- end }}
	)
	{{- if $method.HasResponse -}}
	-> fidl::client2::QueryResponseFut<(
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
	type {{ $method.CamelName }}ResponseFut = fidl::client2::QueryResponseFut<(
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
	), {{ $method.Ordinal }})
	}
	{{- end -}}
	{{- end -}}
}

pub struct {{ $interface.Name }}EventStream {
	event_receiver: fidl::client2::EventReceiver,
}

impl Stream for {{ $interface.Name }}EventStream {
	type Item = {{ $interface.Name }}Event;
	type Error = fidl::Error;

	fn poll_next(&mut self, cx: &mut futures::task::Context)
		-> futures::Poll<Option<Self::Item>, Self::Error>
	{
		let mut buf = match try_ready!(self.event_receiver.poll_next(cx)) {
			Some(buf) => buf,
			None => return Ok(futures::Async::Ready(None)),
		};
		let (bytes, handles) = buf.split_mut();
		let (tx_header, body_bytes) = fidl::encoding2::decode_transaction_header(bytes)?;
		match tx_header.ordinal {
			{{- range $method := $interface.Methods }}
			{{- if not $method.HasRequest }}
			{{ $method.Ordinal }} => {
				let mut out_tuple: (
					{{- range $index, $param := $method.Response -}}
					{{- if ne 0 $index -}}, {{- $param.Type -}}
					{{- else -}} {{- $param.Type -}}
					{{- end -}}
					{{- end -}}
				) = fidl::encoding2::Decodable::new_empty();
				fidl::encoding2::Decoder::decode_into(body_bytes, handles, &mut out_tuple)?;
				Ok(futures::Async::Ready(Some(
					{{ $interface.Name }}Event::{{ $method.CamelName }} {
						{{- range $index, $param := $method.Response -}}
						{{- if ne 1 (len $method.Response) -}}
							{{- $param.Name -}}: out_tuple.{{- $index -}},
						{{- else -}}
							{{- $param.Name -}}: out_tuple,
						{{- end -}}
						{{- end -}}
					}
				)))
			}
			{{- end -}}
			{{- end }}
			_ => Err(fidl::Error::UnknownOrdinal {
				ordinal: tx_header.ordinal,
				service_name: <{{ $interface.Name }}Marker as fidl::endpoints2::ServiceMarker>::NAME,
			})
		}
	}
}

#[derive(Debug)]
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

pub trait {{ $interface.Name }} {
	type OnOpenFut: Future<Item = (), Error = Never> + Send;
	fn on_open(&mut self, control_handle: {{ $interface.Name }}ControlHandle) -> Self::OnOpenFut;

	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	type {{ $method.CamelName }}Fut: Future<Item = (), Error = Never> + Send;
	fn {{ $method.Name }} (&mut self,
		{{- range $request := $method.Request }}
		{{ $request.Name }}: {{ $request.Type }},
		{{- end }}
		{{- if $method.HasResponse }}
		response_chan: {{ $interface.Name }}{{ $method.CamelName }}Responder,
		{{- else }}
		control_handle: {{ $interface.Name }}ControlHandle
		{{- end }}
	) -> Self::{{ $method.CamelName }}Fut;
	{{ end -}}
	{{- end }}

	fn serve(mut self, channel: async::Channel)
		-> {{ $interface.Name }}Server<Self>
	where Self: Sized
	{
		let inner = ::std::sync::Arc::new(fidl::ServeInner::new(channel));
		let on_open_fut = self.on_open(
			{{ $interface.Name }}ControlHandle {
				inner: inner.clone(),
			}
		);
		{{ $interface.Name }}Server {
			server: self,
			inner: inner.clone(),
			msg_buf: zx::MessageBuf::new(),
			on_open_fut: Some(on_open_fut),
			{{- range $method := $interface.Methods }}
			{{- if $method.HasRequest -}}
			{{ $method.Name }}_futures: futures::stream::FuturesUnordered::new(),
			{{- end -}}
			{{- end }}
		}
	}
}

pub struct {{ $interface.Name }}Server<T: {{ $interface.Name }}> {
	server: T,
	inner: ::std::sync::Arc<fidl::ServeInner>,
	msg_buf: zx::MessageBuf,
	on_open_fut: Option<T::OnOpenFut>,
	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest -}}
	{{ $method.Name }}_futures: futures::stream::FuturesUnordered<T::{{ $method.CamelName }}Fut>,
	{{- end -}}
	{{- end }}
}

impl<T: {{ $interface.Name }}> {{ $interface.Name }}Server<T> {
	pub fn control_handle(&self) -> {{ $interface.Name }}ControlHandle {
		{{ $interface.Name }}ControlHandle {
			inner: self.inner.clone(),
		}
	}
}

impl<T: {{ $interface.Name }}> futures::Future for {{ $interface.Name }}Server<T> {
	type Item = ();
	type Error = fidl::Error;

	fn poll(&mut self, cx: &mut futures::task::Context) -> futures::Poll<Self::Item, Self::Error> { loop {
		let mut made_progress_this_loop_iter = false;

		if self.inner.poll_shutdown(cx) {
			return Ok(futures::Async::Ready(()));
		}

		let completed_on_open = if let Some(on_open_fut) = &mut self.on_open_fut {
			match on_open_fut.poll(cx) {
				Ok(futures::Async::Ready(())) => true,
				Ok(futures::Async::Pending) => false,
				Err(never) => match never {},
			}
		} else {
			false
		};

		if completed_on_open {
			made_progress_this_loop_iter = true;
			self.on_open_fut.take();
		}

		{{- range $method := $interface.Methods }}
		{{- if $method.HasRequest -}}
		match self.{{ $method.Name }}_futures.poll_next(cx).map_err(|never| match never {})? {
			futures::Async::Ready(Some(())) => made_progress_this_loop_iter = true,
			_ => {},
		}
		{{- end -}}
		{{- end }}

		match self.inner.channel().recv_from(&mut self.msg_buf, cx) {
			Ok(futures::Async::Ready(())) => {},
			Ok(futures::Async::Pending) => {
				if !made_progress_this_loop_iter {
					return Ok(futures::Async::Pending);
				} else {
					continue;
				}
			}
			Err(zx::Status::PEER_CLOSED) => {
				return Ok(futures::Async::Ready(()));
			}
			Err(e) => return Err(fidl::Error::ServerRequestRead(e)),
		}

		{
			// A message has been received from the channel
			let (bytes, handles) = self.msg_buf.split_mut();
			let (header, body_bytes) = fidl::encoding2::decode_transaction_header(bytes)?;

			match header.ordinal {
				{{- range $method := $interface.Methods }}
					{{- if $method.HasRequest }}
					{{ $method.Ordinal }} => {
						let mut req: (
							{{- range $index, $param := $method.Request -}}
								{{- if ne 0 $index -}}, {{- $param.Type -}}
								{{- else -}} {{- $param.Type -}}
								{{- end -}}
							{{- end -}}
						) = fidl::encoding2::Decodable::new_empty();
						fidl::encoding2::Decoder::decode_into(body_bytes, handles, &mut req)?;
						let control_handle = {{ $interface.Name }}ControlHandle {
							inner: self.inner.clone(),
						};
						self.{{ $method.Name }}_futures.push(
							self.server.{{ $method.Name }}(
								{{- range $index, $param := $method.Request -}}
									{{- if ne 1 (len $method.Request) -}}
									req.{{ $index }},
									{{- else -}}
									req,
									{{- end -}}
								{{- end -}}
								{{- if $method.HasResponse -}}
									{{- $interface.Name -}}{{- $method.CamelName -}}Responder {
										control_handle,
										tx_id: header.tx_id,
									}
									{{- else -}}
									control_handle
								{{- end -}}
							)
						);
					}
					{{- end }}
				{{- end }}
				// TODO(cramertj) handle control/fileio messages
				_ => return Err(fidl::Error::UnknownOrdinal {
					ordinal: header.ordinal,
					service_name: "unknown fidl2", // TODO(cramertj)
				}),
			}
		}
		self.msg_buf.clear();
	}}
}

pub struct {{ $interface.Name }}RequestStream {
	inner: ::std::sync::Arc<fidl::ServeInner>,
	msg_buf: zx::MessageBuf,
}

impl fidl::endpoints2::RequestStream for {{ $interface.Name }}RequestStream {
	fn from_channel(channel: async::Channel) -> Self {
		Self {
			inner: ::std::sync::Arc::new(fidl::ServeInner::new(channel)),
			msg_buf: zx::MessageBuf::new(),
		}
	}

	type ControlHandle = {{ $interface.Name }}ControlHandle;
	fn control_handle(&self) -> Self::ControlHandle {
		{{ $interface.Name }}ControlHandle { inner: self.inner.clone() }
	}
}

impl Stream for {{ $interface.Name }}RequestStream {
	type Item = {{ $interface.Name }}Request;
	type Error = fidl::Error;
	fn poll_next(&mut self, cx: &mut futures::task::Context)
		-> futures::Poll<Option<Self::Item>, Self::Error>
	{
		if self.inner.poll_shutdown(cx) {
			return Ok(futures::Async::Ready(None));
		}
		match self.inner.channel().recv_from(&mut self.msg_buf, cx) {
			Ok(futures::Async::Ready(())) => {},
			Ok(futures::Async::Pending) => return Ok(futures::Async::Pending),
			Err(zx::Status::PEER_CLOSED) => {
				return Ok(futures::Async::Ready(None));
			}
			Err(e) => return Err(fidl::Error::ServerRequestRead(e)),
		}

		let res = {
			// A message has been received from the channel
			let (bytes, handles) = self.msg_buf.split_mut();
			let (header, body_bytes) = fidl::encoding2::decode_transaction_header(bytes)?;

			Ok(futures::Async::Ready(Some(match header.ordinal {
				{{- range $method := $interface.Methods }}
				{{- if $method.HasRequest }}
				{{ $method.Ordinal }} => {
					let mut req: (
						{{- range $index, $param := $method.Request -}}
							{{- if ne 0 $index -}}, {{- $param.Type -}}
							{{- else -}} {{- $param.Type -}}
							{{- end -}}
						{{- end -}}
					) = fidl::encoding2::Decodable::new_empty();
					fidl::encoding2::Decoder::decode_into(body_bytes, handles, &mut req)?;
					let control_handle = {{ $interface.Name }}ControlHandle {
						inner: self.inner.clone(),
					};

					{{ $interface.Name }}Request::{{ $method.CamelName }} {
						{{- range $index, $param := $method.Request -}}
							{{- if ne 1 (len $method.Request) -}}
							{{ $param.Name }}: req.{{ $index }},
							{{- else -}}
							{{ $param.Name }}: req,
							{{- end -}}
						{{- end -}}
						{{- if $method.HasResponse -}}
							responder: {{- $interface.Name -}}{{- $method.CamelName -}}Responder {
								control_handle,
								tx_id: header.tx_id,
							},
							{{- else -}}
							control_handle,
						{{- end -}}
					}
				}
				{{- end }}
				{{- end }}
				_ => return Err(fidl::Error::UnknownOrdinal {
					ordinal: header.ordinal,
					service_name: <{{ $interface.Name }}Marker as fidl::endpoints2::ServiceMarker>::NAME,
				}),
			})))
		};

		self.msg_buf.clear();
		res
	}
}

pub enum {{ $interface.Name }}Request {
	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
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

pub struct {{ $interface.Name }}Impl<
	{{ template "GenerateImplGenerics" $interface }}
> {
	pub state: State,
	pub on_open: OnOpen,
	{{- range $method := $interface.Methods -}}
	{{ if $method.HasRequest }}
	pub {{ $method.Name }}: {{ $method.CamelName }},
	{{- end -}}
	{{- end }}
}

impl<
	{{ template "GenerateImplGenerics" $interface }}
> {{ $interface.Name }} for {{ $interface.Name }}Impl<State, OnOpen, OnOpenFut,
	{{- range $method := $interface.Methods -}}
	{{ if $method.HasRequest }}
	{{ $method.CamelName }},
	{{ $method.CamelName -}}Fut,
	{{- end }}
	{{- end }}
>
{
	type OnOpenFut = OnOpenFut;
	fn on_open(&mut self, response_chan: {{ $interface.Name}}ControlHandle) -> Self::OnOpenFut {
		(self.on_open)(&mut self.state, response_chan)
	}

	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	type {{ $method.CamelName }}Fut = {{ $method.CamelName }}Fut;
	fn {{ $method.Name }} (&mut self,
		{{- range $request := $method.Request }}
		{{ $request.Name }}: {{ $request.Type }},
		{{- end }}
		{{- if $method.HasResponse }}
		response_chan: {{ $interface.Name }}{{ $method.CamelName}}Responder
		{{- else }}
		response_chan: {{ $interface.Name }}ControlHandle
		{{- end }}
	) -> Self::{{ $method.CamelName }}Fut
	{
		(self.{{ $method.Name }})(
			&mut self.state,
			{{- range $request := $method.Request }}
			{{ $request.Name }},
			{{- end }}
			response_chan
		)
	}
	{{ end -}}
	{{- end }}
}

#[derive(Clone)]
pub struct {{ $interface.Name }}ControlHandle {
	inner: ::std::sync::Arc<fidl::ServeInner>,
}

impl {{ $interface.Name }}ControlHandle {
	pub fn shutdown(&self) {
		self.inner.shutdown()
	}

	{{- range $method := $interface.Methods }}
	{{- if not $method.HasRequest }}
	pub fn send_{{ $method.Name }}(&self
		{{- range $param := $method.Response -}},
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}}
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let header = fidl::encoding2::TransactionHeader {
			tx_id: 0,
			flags: 0,
			ordinal: {{ $method.Ordinal }},
		};

		let mut response = (
			{{- range $index, $param := $method.Response -}}
				{{- if ne 0 $index -}}, {{ $param.Name -}}
				{{- else -}} {{ $param.Name -}}
				{{- end -}}
			{{- end -}}
		);

		let mut msg = fidl::encoding2::TransactionMessage {
			header,
			body: &mut response,
		};

		let (bytes, handles) = (&mut vec![], &mut vec![]);
		fidl::encoding2::Encoder::encode(bytes, handles, &mut msg)?;
		self.inner.channel().write(&*bytes, &mut *handles).map_err(fidl::Error::ServerResponseWrite)?;
		Ok(())
	}
	{{ end -}}
	{{- end -}}
}

/* beginning of response types */
{{- range $method := $interface.Methods }}
{{- if and $method.HasRequest $method.HasResponse }}
pub struct {{ $interface.Name }}{{ $method.CamelName }}Responder {
	control_handle: {{ $interface.Name }}ControlHandle,
	tx_id: u32,
}

impl {{ $interface.Name }}{{ $method.CamelName }}Responder {
	pub fn control_handle(&self) -> &{{ $interface.Name }}ControlHandle {
		&self.control_handle
	}

	pub fn send_or_shutdown(&self,
		{{- range $param := $method.Response -}}
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}},
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let r = self.send(
			{{- range $index, $param := $method.Response -}}
			{{ $param.Name -}},
			{{- end -}}
		);
		if r.is_err() {
			self.control_handle().shutdown();
		}
		r
	}

	pub fn send(&self,
		{{- range $param := $method.Response -}}
		mut {{ $param.Name -}}: {{ $param.BorrowedType -}},
		{{- end -}}
	) -> Result<(), fidl::Error> {
		let header = fidl::encoding2::TransactionHeader {
			tx_id: self.tx_id,
			flags: 0,
			ordinal: {{ $method.Ordinal }},
		};

		let mut response = (
			{{- range $index, $param := $method.Response -}}
			{{- if ne 0 $index -}}, {{ $param.Name -}}
			{{- else -}} {{ $param.Name -}}
			{{- end -}}
			{{- end -}}
		);

		let mut msg = fidl::encoding2::TransactionMessage {
			header,
			body: &mut response,
		};

		let (bytes, handles) = (&mut vec![], &mut vec![]);
		fidl::encoding2::Encoder::encode(bytes, handles, &mut msg)?;
		self.control_handle.inner.channel().write(&*bytes, &mut *handles).map_err(fidl::Error::ServerResponseWrite)?;
		Ok(())
	}
}
{{- end -}}
{{- end -}}
{{- end -}}
`
