// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.tracing.controller/cpp/fidl.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/socket.h>
#include <stdlib.h>

#include <gtest/gtest.h>
#include <trace-test-utils/read_records.h>

#include "lib/syslog/cpp/macros.h"

TEST(PerfettoBridgeIntegrationTest, Init) {
  zx::result client_end = component::Connect<fuchsia_tracing_controller::Controller>();
  ASSERT_TRUE(client_end.is_ok());
  const fidl::SyncClient client{std::move(*client_end)};

  zx::socket in_socket;
  zx::socket outgoing_socket;

  ASSERT_EQ(zx::socket::create(0u, &in_socket, &outgoing_socket), ZX_OK);
  const fuchsia_tracing_controller::TraceConfig config{
      {.buffer_size_megabytes_hint = uint32_t{4},
       .buffering_mode = fuchsia_tracing_controller::BufferingMode::kOneshot}};
  auto init_response = client->InitializeTracing({config, std::move(outgoing_socket)});
  ASSERT_TRUE(init_response.is_ok());

  client->StartTracing({});
  sleep(2);
  client->StopTracing({{{.write_results = true}}});

  uint8_t buffer[1024];
  size_t actual;
  ASSERT_EQ(in_socket.read(0, buffer, 1024, &actual), ZX_OK);
  ASSERT_GT(actual, size_t{0});
  ASSERT_LT(actual, size_t{1024});
  FX_LOGS(INFO) << "Socket read " << actual << " bytes of trace data.";

  bool saw_perfetto_blob = false;
  trace::TraceReader::RecordConsumer handle_perfetto_blob =
      [&saw_perfetto_blob](trace::Record record) {
        if (record.type() == trace::RecordType::kBlob &&
            record.GetBlob().type == TRACE_BLOB_TYPE_PERFETTO) {
          saw_perfetto_blob = true;
        }
      };
  trace::TraceReader reader(std::move(handle_perfetto_blob), [](fbl::String) {});
  trace::Chunk data{reinterpret_cast<uint64_t*>(buffer), actual >> 3};
  reader.ReadRecords(data);
  EXPECT_TRUE(saw_perfetto_blob);
}
