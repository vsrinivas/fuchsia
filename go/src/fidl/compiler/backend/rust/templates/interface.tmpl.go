// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "GenerateImplGenerics" -}}
{{- $interface := . -}}
State,
{{- range $method := $interface.Methods }}
  {{- if $method.HasRequest }}
  {{ $method.CamelName }}: FnMut(&mut State,
  {{- range $request := $method.Request -}} {{ $request.Type -}},{{- end -}}
  {{- if $method.HasResponse -}}
  {{- $interface.Name -}}{{- $method.CamelName -}}Response
  {{- else }}
  {{ $interface.Name -}}Controller_ask_cramertj_to_fix_naming_now
  {{- end }}) -> {{ $method.CamelName }}Fut,
  {{ $method.CamelName }}Fut: Future<Item = (), Error = Never>,
  {{- end -}}
{{- end -}}
{{- end -}}

{{- define "InterfaceDeclaration" -}}
{{- $interface := . }}

#[derive(Debug, Clone)]
pub struct {{ $interface.Name }}Marker;

impl fidl::endpoints2::ServiceMarker for {{ $interface.Name }}Marker {
  type Proxy = {{ $interface.Name }}Proxy;
  const NAME: &'static str = "{{ $interface.ServiceName }}";
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

impl {{ $interface.Name }}Proxy {
  pub fn new(channel: async::Channel) -> Self {
   Self { client: fidl::client2::Client::new(channel) }
  }
  {{- range $method := $interface.Methods }}
  {{- if $method.HasRequest }}
  pub fn {{ $method.Name }}(&self,
   {{- range $request := $method.Request }}
   mut {{ $request.Name }}: &mut {{ $request.Type }},
   {{- end }}
  )
  {{- if $method.HasResponse -}}
  -> fidl::client2::QueryResponseFut<(
   {{- range $index, $response := $method.Response -}}
   {{- if (eq $index 0) -}} {{ $response.Type }}
   {{- else -}}, {{ $response.Type }} {{- end -}}
   {{- end -}}
  )> {
   self.client.send_query(&mut (
  {{- else -}}
  -> Result<(), fidl::Error> {
   self.client.send(&mut (
  {{- end -}}
    {{- range $index, $request := $method.Request -}}
    {{- if (eq $index 0) -}} &mut *{{ $request.Name }}
    {{- else -}}, &mut *{{ $request.Name }} {{- end -}}
    {{- end -}}
   ), {{ $method.Ordinal }})
  }
  {{- end -}}
  {{- end -}}
}

pub trait {{ $interface.Name }} {
  {{- range $method := $interface.Methods }}
  {{- if $method.HasRequest }}
  type {{ $method.CamelName }}Fut: Future<Item = (), Error = Never>;
  fn {{ $method.Name }} (&mut self,
   {{- range $request := $method.Request }}
   {{ $request.Name }}: {{ $request.Type }},
   {{- end }}
   {{- if $method.HasResponse }}
   response_chan: {{ $interface.Name }}{{ $method.CamelName }}Response,
   {{- else }}
   controller: {{ $interface.Name }}Controller_ask_cramertj_to_fix_naming_now
   {{- end }}
  ) -> Self::{{ $method.CamelName }}Fut;
  {{ end -}}
  {{- end }}

  fn serve(self, channel: async::Channel)
    -> {{ $interface.Name }}Server<Self>
   where Self: Sized
  {
    {{ $interface.Name }}Server {
      server: self,
      channel: ::std::sync::Arc::new(channel),
      msg_buf: zx::MessageBuf::new(),
      {{- range $method := $interface.Methods }}
      {{ $method.Name }}_futures: futures::stream::FuturesUnordered::new(),
      {{- end }}
    }
  }
}

pub struct {{ $interface.Name }}Server<T: {{ $interface.Name }}> {
  server: T,
  channel: ::std::sync::Arc<async::Channel>,
  msg_buf: zx::MessageBuf,
  {{- range $method := $interface.Methods }}
  {{ $method.Name }}_futures: futures::stream::FuturesUnordered<T::{{ $method.CamelName }}Fut>,
  {{- end }}
}

impl<T: {{ $interface.Name }}> futures::Future for {{ $interface.Name }}Server<T> {
  type Item = ();
  type Error = fidl::Error;

  fn poll(&mut self, cx: &mut futures::task::Context) -> futures::Poll<Self::Item, Self::Error> {
   loop {
    let mut made_progress_this_loop_iter = false;

    {{- range $method := $interface.Methods }}
    match self.{{ $method.Name }}_futures.poll_next(cx).map_err(|never| match never {})? {
      futures::Async::Ready(Some(())) => made_progress_this_loop_iter = true,
      _ => {},
    }
    {{- end }}

    match self.channel.recv_from(&mut self.msg_buf, cx) {
      Ok(futures::Async::Ready(())) => {},
      Ok(futures::Async::Pending) => {
       if !made_progress_this_loop_iter {
        return Ok(futures::Async::Pending);
       } else {
        continue;
       }
      }
      Err(zx::Status::PEER_CLOSED) => {
       // TODO(cramertj): propagate to on_closed handler rather than just stopping
      }
      Err(e) => return Err(fidl::Error::ServerRequestRead(e)),
    }

    {
    // A message has been received from the channel
    let (bytes, handles) = self.msg_buf.split_mut();
    let (header, body_bytes) = fidl::encoding2::decode_transaction_header(bytes)?;

    match header.ordinal {
      {{- range $method := $interface.Methods }}
      {{ $method.Ordinal }} => {
       let mut req: (
        {{- range $index, $param := $method.Request -}}
          {{- if ne 0 $index -}}, {{- $param.Type -}}
          {{- else -}} {{- $param.Type -}}
          {{- end -}}
        {{- end -}}
       ) = fidl::encoding2::Decodable::new_empty();
       fidl::encoding2::Decoder::decode_into(body_bytes, handles, &mut req)?;
       let controller = {{ $interface.Name }}Controller_ask_cramertj_to_fix_naming_now{};

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
        {{- $interface.Name -}}{{- $method.CamelName -}}Response {
          controller,
          tx_id: header.tx_id,
          channel: self.channel.clone(),
        }
        {{- else -}}
        controller
        {{- end -}}
        )
       );
      }
      {{- end }}
      // TODO(cramertj) handle control/fileio messages
      _ => return Err(fidl::Error::UnknownOrdinal {
       ordinal: header.ordinal,
       service_name: "unknown fidl2", // TODO(cramertj)
      }),
    }
    }
    self.msg_buf.clear();
   }
  }
}

pub struct {{ $interface.Name }}Impl<
  {{ template "GenerateImplGenerics" $interface }}
> {
  pub state: State,
  {{- range $method := $interface.Methods -}}
  {{ if $method.HasRequest }}
  pub {{ $method.Name }}: {{ $method.CamelName }},
  {{- end -}}
  {{- end }}
}

impl<
  {{ template "GenerateImplGenerics" $interface }}
> {{ $interface.Name }} for {{ $interface.Name }}Impl<State,
  {{- range $method := $interface.Methods -}}
  {{ if $method.HasRequest }}
  {{ $method.CamelName }},
  {{ $method.CamelName }}Fut,
  {{- end -}}
  {{- end }}
>
{
  {{- range $method := $interface.Methods }}
  {{- if $method.HasRequest }}
  type {{ $method.CamelName }}Fut = {{ $method.CamelName }}Fut;
  fn {{ $method.Name }} (&mut self,
   {{- range $request := $method.Request }}
   {{ $request.Name }}: {{ $request.Type }},
   {{- end }}
   {{- if $method.HasResponse }}
   response_chan: {{ $interface.Name }}{{ $method.CamelName}}Response
   {{- else }}
   response_chan: {{ $interface.Name }}Controller_ask_cramertj_to_fix_naming_now
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

pub struct {{ $interface.Name }}Controller_ask_cramertj_to_fix_naming_now {
  // TODO(cramertj): add Arc<fidl::server2::ServerPool> for reusable message buffers
  // TODO(cramertj): add ability to kill channel (w/ optional epitaph), send events
}

/* beginning of response types */
{{- range $method := $interface.Methods }}
{{- if $method.HasResponse }}
pub struct {{ $interface.Name }}{{ $method.CamelName }}Response {
  controller: {{ $interface.Name }}Controller_ask_cramertj_to_fix_naming_now,
  tx_id: u32,
  channel: ::std::sync::Arc<async::Channel>,
}

impl {{ $interface.Name }}{{ $method.CamelName }}Response {
  // TODO(cramertj): add ability to kill channel (w/ optional epitaph), send events
  // (by delegating to self.controller)

  pub fn send(&self,
   {{- range $param := $method.Response -}}
   {{- $param.Name -}}: &mut {{ $param.Type -}},
   {{- end -}}
  ) -> Result<(), fidl::Error> {
   let header = fidl::encoding2::TransactionHeader {
    tx_id: self.tx_id,
    flags: 0,
    ordinal: {{ $method.Ordinal }},
   };

   let mut response = (
    {{- range $index, $param := $method.Response -}}
      {{- if ne 0 $index -}}, {{- $param.Name -}}
      {{- else -}} {{- $param.Name -}}
      {{- end -}}
    {{- end -}}
   );

   let mut msg = fidl::encoding2::TransactionMessage {
    header,
    body: &mut response,
   };

   let (bytes, handles) = (&mut vec![], &mut vec![]);
   fidl::encoding2::Encoder::encode(bytes, handles, &mut msg)?;
   self.channel.write(&*bytes, &mut *handles).map_err(fidl::Error::ServerResponseWrite)?;
   Ok(())
  }
}
{{- end -}}
{{- end -}}
{{- end -}}
`
