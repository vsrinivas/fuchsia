// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rust

import (
	"bytes"
	"fmt"
	"strings"
	"text/template"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
)

var benchmarkTmpl = template.Must(template.New("benchmarkTmpls").Parse(`
#![allow(unused_imports)]
use {
	fidl::{
		encoding::{Context, Decodable, Decoder, Encoder, with_tls_coding_bufs},
		handle::Handle,
	},
	fidl_benchmarkfidl{{ .CrateSuffix }} as benchmarkfidl{{ .CrateSuffix }},
	fuchsia_criterion::criterion::{BatchSize, Bencher},
	fuchsia_async::futures::{future, stream::StreamExt},
	fuchsia_zircon as zx,
	gidl_util::{copy_handle, copy_handles_at, disown_handles},
};

// BENCHMARKS is aggregated by a generated benchmark_suite.rs file, which is ultimately
// used in benchmarks in src/tests/benchmarks/fidl/rust/src/main.rs.
pub const BENCHMARKS: [(&'static str, fn(&mut Bencher)); {{ .NumBenchmarks }}] = [
{{- range .Benchmarks }}
	("Builder/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_builder),
	("Encode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_encode),
	("Decode/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_decode),
	{{ if .EnableSendEventBenchmark }}
	("SendEvent/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_send_event),
	{{- end -}}
	{{ if .EnableEchoCallBenchmark }}
	("EchoCall/{{ .ChromeperfPath }}", benchmark_{{ .Name }}_echo_call),
	{{- end -}}
{{- end }}
];

const _V1_CONTEXT: &Context = &Context {};

{{ range .Benchmarks }}
fn benchmark_{{ .Name }}_builder(b: &mut Bencher) {
	b.iter(|| {
		{{ .Value }}
	});
}

fn benchmark_{{ .Name }}_encode(b: &mut Bencher) {
	b.iter_batched_ref(
		|| {
			{{- if .HandleDefs }}
			let handle_defs = vec!{{ .HandleDefs }};
			let handle_defs = unsafe { disown_handles(handle_defs) };
			let handle_defs = handle_defs.as_ref();
			let value = {{ .Value }};
			value
			{{- else }}
			{{ .Value }}
			{{- end }}
		},
		|value| {
			{{- /* Encode to TLS buffers since that's what the bindings do in practice. */}}
			with_tls_coding_bufs(|bytes, handles| {
				Encoder::encode_with_context(_V1_CONTEXT, bytes, handles, value).unwrap();
			});
		},
		BatchSize::SmallInput,
	);
}

fn benchmark_{{ .Name }}_decode(b: &mut Bencher) {
	{{- if .HandleDefs }}
	b.iter_batched_ref(
		|| {
			let handle_defs = vec!{{ .HandleDefs }};
			let handle_defs = unsafe { disown_handles(handle_defs) };
			let handle_defs = handle_defs.as_ref();
			let original_value = &mut {{ .Value }};
			let bytes = &mut Vec::<u8>::new();
			let handles = &mut Vec::<Handle>::new();
			Encoder::encode_with_context(_V1_CONTEXT, bytes, handles, original_value).unwrap();
			(bytes, handles, {{ .ValueType }}::new_empty())
		},
		|(bytes, handles, value)| {
			Decoder::decode_with_context(_V1_CONTEXT, bytes, handles, value).unwrap();
		},
		BatchSize::SmallInput,
	);
	{{- else }}
	let bytes = &mut Vec::<u8>::new();
	let handles = &mut Vec::<Handle>::new();
	let original_value = &mut {{ .Value }};
	Encoder::encode_with_context(_V1_CONTEXT, bytes, handles, original_value).unwrap();
	b.iter_batched_ref(
		{{- /* We use a fresh empty value for each run rather than decoding into
			   the same value every time. The latter would be less realistic
			   since e.g. vectors would only allocate on the first run. */}}
		{{ .ValueType }}::new_empty,
		|value| {
			Decoder::decode_with_context(_V1_CONTEXT, bytes, handles, value).unwrap();
		},
		BatchSize::SmallInput,
	);
	{{- end }}
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
{{ end }}
`))

type benchmarkTmplInput struct {
	NumBenchmarks int
	CrateSuffix   string
	Benchmarks    []benchmark
}
type benchmark struct {
	Name, ChromeperfPath, HandleDefs, Value, ValueType string
	EnableSendEventBenchmark, EnableEchoCallBenchmark  bool
}

// GenerateBenchmarks generates Rust benchmarks.
func GenerateBenchmarks(gidl gidlir.All, fidl fidlir.Root, config gidlconfig.GeneratorConfig) ([]byte, map[string][]byte, error) {
	schema := gidlmixer.BuildSchema(fidl)
	var benchmarks []benchmark
	nBenchmarks := 0
	for _, gidlBenchmark := range gidl.Benchmark {
		decl, err := schema.ExtractDeclaration(gidlBenchmark.Value, gidlBenchmark.HandleDefs)
		if err != nil {
			return nil, nil, fmt.Errorf("benchmark %s: %s", gidlBenchmark.Name, err)
		}
		value := visit(gidlBenchmark.Value, decl)
		benchmarks = append(benchmarks, benchmark{
			Name:                     benchmarkName(gidlBenchmark.Name),
			ChromeperfPath:           gidlBenchmark.Name,
			HandleDefs:               buildHandleDefs(gidlBenchmark.HandleDefs),
			Value:                    value,
			ValueType:                declName(decl),
			EnableSendEventBenchmark: gidlBenchmark.EnableSendEventBenchmark,
			EnableEchoCallBenchmark:  gidlBenchmark.EnableEchoCallBenchmark,
		})
		nBenchmarks += 3
		if gidlBenchmark.EnableSendEventBenchmark {
			nBenchmarks++
		}
		if gidlBenchmark.EnableEchoCallBenchmark {
			nBenchmarks++
		}
	}
	input := benchmarkTmplInput{
		NumBenchmarks: nBenchmarks,
		CrateSuffix:   config.RustBenchmarksFidlLibrary,
		Benchmarks:    benchmarks,
	}
	var buf bytes.Buffer
	err := benchmarkTmpl.Execute(&buf, input)
	return buf.Bytes(), nil, err
}

func benchmarkName(gidlName string) string {
	return fidlcommon.ToSnakeCase(strings.ReplaceAll(gidlName, "/", "_"))
}
