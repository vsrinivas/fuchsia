// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/result.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/component_context.h>

#include <regex>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <zxtest/zxtest.h>

namespace {

constexpr auto kInspectPublisherUrl =
    "fuchsia-pkg://fuchsia.com/fuchsia-diagnostics-tests#meta/inspect-publisher.cmx";

const char EXPECTED_DATA[] = R"JSON({
    "data_source": "Inspect",
    "metadata": {
        "component_url": "fuchsia-pkg://fuchsia.com/fuchsia-diagnostics-tests#meta/inspect-publisher.cmx",
        "errors": null,
        "filename": "fuchsia.inspect.Tree",
        "timestamp": TIMESTAMP
    },
    "moniker": "inspect-publisher.cmx",
    "payload": {
        "root": {
            "arrays": {
                "doubles": [
                    0.0,
                    0.0,
                    3.5,
                    0.0
                ],
                "ints": [
                    -1,
                    0
                ],
                "uints": [
                    0,
                    2,
                    0
                ]
            },
            "buffers": {
                "bytes": "b64:AQID",
                "string": "foo"
            },
            "exponential_histograms": {
                "double": {
                    "buckets": [
                        {
                            "count": 0.0,
                            "floor": -1.7976931348623157e308,
                            "ceiling": 1.5
                        },
                        {
                            "count": 0.0,
                            "floor": 1.5,
                            "ceiling": 3.5
                        },
                        {
                            "count": 1.0,
                            "floor": 3.5,
                            "ceiling": 8.5
                        },
                        {
                            "count": 0.0,
                            "floor": 8.5,
                            "ceiling": 26.0
                        },
                        {
                            "count": 0.0,
                            "floor": 26.0,
                            "ceiling": 1.7976931348623157e308
                        }
                    ]
                },
                "int": {
                    "buckets": [
                        {
                            "count": 0,
                            "floor": -9223372036854775808,
                            "ceiling": -10
                        },
                        {
                            "count": 0,
                            "floor": -10,
                            "ceiling": -8
                        },
                        {
                            "count": 1,
                            "floor": -8,
                            "ceiling": -4
                        },
                        {
                            "count": 0,
                            "floor": -4,
                            "ceiling": 8
                        },
                        {
                            "count": 0,
                            "floor": 8,
                            "ceiling": 9223372036854775807
                        }
                    ]
                },
                "uint": {
                    "buckets": [
                        {
                            "count": 0,
                            "floor": 0,
                            "ceiling": 1
                        },
                        {
                            "count": 0,
                            "floor": 1,
                            "ceiling": 3
                        },
                        {
                            "count": 1,
                            "floor": 3,
                            "ceiling": 7
                        },
                        {
                            "count": 0,
                            "floor": 7,
                            "ceiling": 19
                        },
                        {
                            "count": 0,
                            "floor": 19,
                            "ceiling": 18446744073709551615
                        }
                    ]
                }
            },
            "linear_histgorams": {
                "double": {
                    "buckets": [
                        {
                            "count": 0.0,
                            "floor": -1.7976931348623157e308,
                            "ceiling": 1.5
                        },
                        {
                            "count": 0.0,
                            "floor": 1.5,
                            "ceiling": 4.0
                        },
                        {
                            "count": 1.0,
                            "floor": 4.0,
                            "ceiling": 6.5
                        },
                        {
                            "count": 0.0,
                            "floor": 6.5,
                            "ceiling": 9.0
                        },
                        {
                            "count": 0.0,
                            "floor": 9.0,
                            "ceiling": 1.7976931348623157e308
                        }
                    ]
                },
                "int": {
                    "buckets": [
                        {
                            "count": 0,
                            "floor": -9223372036854775808,
                            "ceiling": -10
                        },
                        {
                            "count": 0,
                            "floor": -10,
                            "ceiling": -8
                        },
                        {
                            "count": 0,
                            "floor": -8,
                            "ceiling": -6
                        },
                        {
                            "count": 1,
                            "floor": -6,
                            "ceiling": -4
                        },
                        {
                            "count": 0,
                            "floor": -4,
                            "ceiling": 9223372036854775807
                        }
                    ]
                },
                "uint": {
                    "buckets": [
                        {
                            "count": 0,
                            "floor": 0,
                            "ceiling": 1
                        },
                        {
                            "count": 0,
                            "floor": 1,
                            "ceiling": 3
                        },
                        {
                            "count": 1,
                            "floor": 3,
                            "ceiling": 5
                        },
                        {
                            "count": 0,
                            "floor": 5,
                            "ceiling": 7
                        },
                        {
                            "count": 0,
                            "floor": 7,
                            "ceiling": 18446744073709551615
                        }
                    ]
                }
            },
            "numeric": {
                "bool": true,
                "double": 1.5,
                "int": -1,
                "uint": 1
            }
        }
    },
    "version": 1
})JSON";

class AccessorTest : public zxtest::Test {};

// Tests that reading inspect data returns expected data from the archive accessor.
TEST_F(AccessorTest, StreamDiagnosticsInspect) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  auto context = sys::ComponentContext::Create();

  fuchsia::sys::EnvironmentSyncPtr environment;
  EXPECT_EQ(ZX_OK, context->svc()->Connect(environment.NewRequest()));

  fuchsia::sys::LauncherSyncPtr launcher;
  EXPECT_EQ(ZX_OK, environment->GetLauncher(launcher.NewRequest()));

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kInspectPublisherUrl;

  fuchsia::sys::ComponentControllerPtr controller;
  EXPECT_EQ(ZX_OK, launcher->CreateComponent(std::move(launch_info), controller.NewRequest()));

  fuchsia::diagnostics::ArchiveAccessorPtr accessor;
  EXPECT_EQ(ZX_OK, context->svc()->Connect(accessor.NewRequest()));

  inspect::contrib::ArchiveReader reader(
      context->svc()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
      {"inspect-publisher.cmx:root"});

  fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> actual_result;
  fpromise::single_threaded_executor executor;
  executor.schedule_task(
      reader.SnapshotInspectUntilPresent({"inspect-publisher.cmx"})
          .then([&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                        result) mutable { actual_result = std::move(result); }));
  executor.run();

  EXPECT_TRUE(actual_result.is_ok());
  EXPECT_EQ(1, actual_result.value().size());

  auto& data = actual_result.value()[0];
  data.Sort();

  std::regex reg("\"timestamp\": \\d+");
  EXPECT_EQ(EXPECTED_DATA, regex_replace(data.PrettyJson(), reg, "\"timestamp\": TIMESTAMP"));
}

}  // namespace
