// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("benchmarkTmpls").Parse(`
#![allow(unused_imports)]
use {
	fidl::{
		encoding::{Context, Decodable, Decoder, Encoder, with_tls_coding_bufs},
		handle::Handle,
	},
	fidl_benchmarkfidl as benchmarkfidl,
	fuchsia_criterion::criterion::{BatchSize, Bencher},
	fuchsia_async::futures::{future, stream::StreamExt},
	fuchsia_zircon as zx,
};

// BENCHMARKS is aggregated into ALL_BENCHMARKS by lib.rs and ultimately used by
// src/tests/benchmarks/fidl/rust/src/main.rs.
pub const BENCHMARKS: [(&'static str, fn(&mut Bencher)); {{ .NumBenchmarks }}] = [
	("Builder/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_builder),
	("Encode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_encode),
	("Decode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_decode),
	{{ if .EnableSendEventBenchmark }}
	("SendEvent/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_send_event),
	{{- end -}}
	{{ if .EnableEchoCallBenchmark }}
	("EchoCall/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_echo_call),
	{{- end -}}
];

const V1_CONTEXT: &Context = &Context {};

fn benchmark_{{ .Name }}_builder(b: &mut Bencher) {
	b.iter(|| {
		{{ .Value }}
	});
}
fn benchmark_{{ .Name }}_encode(b: &mut Bencher) {
	b.iter_batched_ref(
		|| {
			{{ .Value }}
		},
		|value| {
			{{- /* Encode to TLS buffers since that's what the bindings do in practice. */}}
			with_tls_coding_bufs(|bytes, handles| {
				Encoder::encode_with_context(V1_CONTEXT, bytes, handles, value).unwrap();
			});
		},
		BatchSize::SmallInput,
	);
}
fn benchmark_{{ .Name }}_decode(b: &mut Bencher) {
	let bytes = &mut Vec::<u8>::new();
	{{- /* TODO(fxb/36441): Revisit this when adding support for handles. */}}
	let handles = &mut Vec::<Handle>::new();
	let original_value = &mut {{ .Value }};
	Encoder::encode_with_context(V1_CONTEXT, bytes, handles, original_value).unwrap();

	b.iter_batched_ref(
		{{- /* We use a fresh empty value for each run rather than decoding into
			   the same value every time. The latter would be less realistic
			   since e.g. vectors would only allocate on the first run. */}}
		{{ .ValueType }}::new_empty,
		|value| {
			Decoder::decode_with_context(V1_CONTEXT, bytes, handles, value).unwrap();
		},
		BatchSize::SmallInput,
	);
}
{{ if .EnableSendEventBenchmark }}
async fn {{ .Name }}_send_event_receiver_thread(receiver_fidl_chan_end: zx::Channel, sender_fifo: std::sync::mpsc::SyncSender<()>) {
	let async_receiver_fidl_chan_end = fuchsia_async::Channel::from_channel(receiver_fidl_chan_end).unwrap();
	let proxy = {{ .ValueType }}EventProtocolProxy::new(async_receiver_fidl_chan_end);
	let mut event_stream = proxy.take_event_stream();

	while let Some(_event) = event_stream.next().await {
		sender_fifo.send(()).unwrap();
	};
}
fn benchmark_{{ .Name }}_send_event(b: &mut Bencher) {
	let (sender_fifo, receiver_fifo) = std::sync::mpsc::sync_channel(1);
	let (sender_fidl_chan_end, receiver_fidl_chan_end) = zx::Channel::create().unwrap();
	std::thread::spawn(|| {
		fuchsia_async::Executor::new()
			.unwrap()
			.run_singlethreaded(async move {
			{{ .Name }}_send_event_receiver_thread(receiver_fidl_chan_end, sender_fifo).await;
		});
	});
	fuchsia_async::Executor::new()
		.unwrap()
		.run_singlethreaded(async move {
			let async_sender_fidl_chan_end = fuchsia_async::Channel::from_channel(sender_fidl_chan_end).unwrap();
			let sender = <{{ .ValueType }}EventProtocolRequestStream as fidl::endpoints::RequestStream>::from_channel(async_sender_fidl_chan_end);
			b.iter_batched_ref(|| {
				{{ .Value }}
			},
			|value| {
				fidl::endpoints::RequestStream::control_handle(&sender).send_send_(value).unwrap();
				receiver_fifo.recv().unwrap();
			},
			BatchSize::SmallInput);
		});
}
{{- end -}}
{{ if .EnableEchoCallBenchmark }}
async fn {{ .Name }}_echo_call_server_thread(server_end: zx::Channel) {
	let async_server_end = fuchsia_async::Channel::from_channel(server_end).unwrap();
	let stream = <{{ .ValueType }}EchoCallRequestStream as fidl::endpoints::RequestStream>::from_channel(async_server_end);

    const MAX_CONCURRENT: usize = 10;
    stream.for_each_concurrent(MAX_CONCURRENT, |request| {
		match request {
			Ok({{ .ValueType }}EchoCallRequest::Echo { mut val, responder }) => {
				responder.send(&mut val).unwrap();
			},
			Err(_) => {
				panic!("unexpected err request")
			},
		}
		future::ready(())
	}).await;
}
fn benchmark_{{ .Name }}_echo_call(b: &mut Bencher) {
	let (client_end, server_end) = zx::Channel::create().unwrap();
	std::thread::spawn(|| {
		fuchsia_async::Executor::new()
			.unwrap()
			.run_singlethreaded(async move {
			{{ .Name }}_echo_call_server_thread(server_end).await;
		});
	});
	let mut proxy = {{ .ValueType }}EchoCallSynchronousProxy::new(client_end);
	b.iter_batched_ref(|| {
		{{ .Value }}
	},
	|value| {
		proxy.echo(value, zx::Time::after(zx::Duration::from_seconds(1))).unwrap();
	},
	BatchSize::SmallInput);
}
{{- end -}}
`))

type benchmark struct {
	Name, ChromeperfPath, Value, ValueType            string
	NumBenchmarks                                     int
	EnableSendEventBenchmark, EnableEchoCallBenchmark bool
}

var libTmpl = template.Must(template.New("libTmpls").Parse(`
use fuchsia_criterion::criterion::Bencher;

{{ range .ModuleNames }}
mod {{ . }};
{{ end }}

pub const ALL_BENCHMARKS: [&'static[(&'static str, fn(&mut Bencher))]; {{ len .ModuleNames }}] = [
	{{ range .ModuleNames }}
		&{{ . }}::BENCHMARKS,
	{{ end }}
];`))

type lib struct {
	ModuleNames []string
}

// GenerateBenchmarks generates Rust benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root) ([]byte, map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	files := map[string][]byte{}
	l := lib{}
	var numModules int
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value)
		if err != nil {
			return nil, nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		value := visit(gidlBenchmark.Value, decl)
		nBenchmarks := 3
		if gidlBenchmark.EnableSendEventBenchmark {
			nBenchmarks++
		}
		if gidlBenchmark.EnableEchoCallBenchmark {
			nBenchmarks++
		}
		var buf bytes.Buffer
		if err := benchmarkTmpl.Execute(&buf, benchmark{
			Name:                     benchmarkName(gidlBenchmark.Name),
			NumBenchmarks:            nBenchmarks,
			ChromeperfPath:           gidlBenchmark.Name,
			Value:                    value,
			ValueType:                declName(decl),
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
			EnableEchoCallBenchmark:  gidlBenchmark.EnableEchoCallBenchmark,
		}); err != nil {
			return nil, nil, err
		}
		files[benchmarkName("_"+gidlBenchmark.Name)] = buf.Bytes()
		numModules++
		l.ModuleNames = append(l.ModuleNames, fmt.Sprintf("gidl_generated_benchmark_suite_rust%d", numModules))
	}
	var libBuf bytes.Buffer
	if err := libTmpl.Execute(&libBuf, l); err != nil {
		return nil, nil, err
	}
	return libBuf.Bytes(), files, nil
}

func benchmarkName(gidlName string) string {
	return fidlcommon.ToSnakeCase(strings.ReplaceAll(gidlName, "/", "_"))
}
