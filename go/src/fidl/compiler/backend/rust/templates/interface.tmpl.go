// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "GenerateImplGenerics" -}}
{{- $interface := . -}}
State,
OnOpen: FnMut(&mut State, {{ $interface.Name }}ControlHandle) -> OnOpenFut,
OnOpenFut: Future<Output = ()> + Send,
{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	{{ $method.CamelName }}: FnMut(&mut State,
	{{- range $request := $method.Request -}} {{ $request.Type -}},{{- end -}}
	{{- if $method.HasResponse -}}
	{{- $interface.Name -}}{{- $method.CamelName -}}Responder
	{{- else -}}
	{{ $interface.Name -}}ControlHandle
	{{- end }}) -> {{ $method.CamelName }}Fut,
	{{ $method.CamelName }}Fut: Future<Output = ()> + Send,
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
	type {{ $method.CamelName }}ResponseFut: Future<Output = Result<(
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

impl ::std::marker::Unpin for {{ $interface.Name }}EventStream {}

impl Stream for {{ $interface.Name }}EventStream {
	type Item = Result<{{ $interface.Name }}Event, fidl::Error>;

	fn poll_next(mut self: ::std::mem::PinMut<Self>, cx: &mut futures::task::Context)
		-> futures::Poll<Option<Self::Item>>
	{
		let mut buf = match ready!(self.event_receiver.poll_next_unpin(cx)?) {
			Some(buf) => buf,
			None => return futures::Poll::Ready(None),
		};
		let (bytes, handles) = buf.split_mut();
		let (tx_header, body_bytes) = fidl::encoding2::decode_transaction_header(bytes)?;
		futures::Poll::Ready(Some(match tx_header.ordinal {
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
				ordinal: tx_header.ordinal,
				service_name: <{{ $interface.Name }}Marker as fidl::endpoints2::ServiceMarker>::NAME,
			})
		}))
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
	type OnOpenFut: Future<Output = ()> + Send;
	fn on_open(&mut self, control_handle: {{ $interface.Name }}ControlHandle) -> Self::OnOpenFut;

	{{- range $method := $interface.Methods }}
	{{- if $method.HasRequest }}
	type {{ $method.CamelName }}Fut: Future<Output = ()> + Send;
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

// Safety: only the OnOpen fut is held directly, so it's the only one that
// is projected to, so it's the only one that needs to be Unpin for the Impl
// struct to be Unpin.
impl<T: {{ $interface.Name }}> ::std::marker::Unpin for {{ $interface.Name }}Server<T>
where T::OnOpenFut: ::std::marker::Unpin,
{}

impl<T: {{ $interface.Name }}> {{ $interface.Name }}Server<T> {
	pub fn control_handle(&self) -> {{ $interface.Name }}ControlHandle {
		{{ $interface.Name }}ControlHandle {
			inner: self.inner.clone(),
		}
	}
}

impl<T: {{ $interface.Name }}> futures::Future for {{ $interface.Name }}Server<T> {
	type Output = Result<(), fidl::Error>;

	fn poll(
		mut self: ::std::mem::PinMut<Self>,
		cx: &mut futures::task::Context,
	) -> futures::Poll<Self::Output> {
		// safety: the only potentially !Unpin field is on_open_fut, which we make sure
		// isn't moved below
		let this = unsafe { ::std::mem::PinMut::get_mut_unchecked(self) };
		loop {
		let mut made_progress_this_loop_iter = false;

		if this.inner.poll_shutdown(cx) {
			return futures::Poll::Ready(Ok(()));
		}

		unsafe {
			// Safety: ensure that on_open isn't moved
			let completed_on_open = if let Some(on_open_fut) = &mut this.on_open_fut {
				match ::std::mem::PinMut::new_unchecked(on_open_fut).poll(cx) {
					futures::Poll::Ready(()) => true,
					futures::Poll::Pending => false,
				}
			} else {
				false
			};

			if completed_on_open {
				made_progress_this_loop_iter = true;
				this.on_open_fut = None;
			}
		}

		{{- range $method := $interface.Methods }}
		{{- if $method.HasRequest -}}
		match this.{{ $method.Name }}_futures.poll_next_unpin(cx) {
			futures::Poll::Ready(Some(())) => made_progress_this_loop_iter = true,
			_ => {},
		}
		{{- end -}}
		{{- end }}

		match this.inner.channel().recv_from(&mut this.msg_buf, cx) {
			futures::Poll::Ready(Ok(())) => {},
			futures::Poll::Pending => {
				if !made_progress_this_loop_iter {
					return futures::Poll::Pending;
				} else {
					continue;
				}
			}
			futures::Poll::Ready(Err(zx::Status::PEER_CLOSED)) => {
				return futures::Poll::Ready(Ok(()));
			}
			futures::Poll::Ready(Err(e)) =>
				return futures::Poll::Ready(Err(fidl::Error::ServerRequestRead(e))),
		}

		{
			// A message has been received from the channel
			let (bytes, handles) = this.msg_buf.split_mut();
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
							inner: this.inner.clone(),
						};
						this.{{ $method.Name }}_futures.push(
							this.server.{{ $method.Name }}(
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
				_ => return futures::Poll::Ready(Err(fidl::Error::UnknownOrdinal {
					ordinal: header.ordinal,
					service_name: "unknown fidl2", // TODO(cramertj)
				})),
			}
		}
		this.msg_buf.clear();
	}}
}

pub struct {{ $interface.Name }}RequestStream {
	inner: ::std::sync::Arc<fidl::ServeInner>,
	msg_buf: zx::MessageBuf,
}

impl ::std::marker::Unpin for {{ $interface.Name }}RequestStream {}

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
	type Item = Result<{{ $interface.Name }}Request, fidl::Error>;

	fn poll_next(mut self: ::std::mem::PinMut<Self>, cx: &mut futures::task::Context)
		-> futures::Poll<Option<Self::Item>>
	{
		let this = &mut *self;
		if this.inner.poll_shutdown(cx) {
			return futures::Poll::Ready(None);
		}
		match this.inner.channel().recv_from(&mut this.msg_buf, cx) {
			futures::Poll::Ready(Ok(())) => {},
			futures::Poll::Pending => return futures::Poll::Pending,
			futures::Poll::Ready(Err(zx::Status::PEER_CLOSED)) =>
				return futures::Poll::Ready(None),
			futures::Poll::Ready(Err(e)) =>
				return futures::Poll::Ready(Some(Err(fidl::Error::ServerRequestRead(e)))),
		}

		let res = {
			// A message has been received from the channel
			let (bytes, handles) = this.msg_buf.split_mut();
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
						inner: this.inner.clone(),
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
				_ => return futures::Poll::Ready(Some(Err(fidl::Error::UnknownOrdinal {
					ordinal: header.ordinal,
					service_name: <{{ $interface.Name }}Marker as fidl::endpoints2::ServiceMarker>::NAME,
				}))),
			}
		};

		this.msg_buf.clear();
		futures::Poll::Ready(Some(Ok(res)))
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

// Unpin is never projected for the Impl struct
impl<
	{{ template "GenerateImplGenerics" $interface }}
> ::std::marker::Unpin for {{ $interface.Name }}Impl<State, OnOpen, OnOpenFut,
	{{- range $method := $interface.Methods -}}
	{{ if $method.HasRequest }}
	{{ $method.CamelName }},
	{{ $method.CamelName -}}Fut,
	{{- end }}
	{{- end }}
>
{}

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
#[must_use = "FIDL methods require a response to be sent"]
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
