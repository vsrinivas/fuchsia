// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/start_args.h"

#include <gtest/gtest.h>

namespace fdata = fuchsia_data;
namespace fdf = fuchsia_driver_framework;
namespace frunner = fuchsia_component_runner;

TEST(StartArgsTest, SymbolValue) {
  fidl::Arena allocator;
  fidl::VectorView<fdf::wire::NodeSymbol> symbol_entries(allocator, 1);
  symbol_entries[0].Allocate(allocator);
  symbol_entries[0].set_name(allocator, "sym").set_address(allocator, 0xfeed);

  EXPECT_EQ(0xfeedu, *driver::SymbolValue<zx_vaddr_t>(symbol_entries, "sym"));
  EXPECT_EQ(ZX_ERR_NOT_FOUND,
            driver::SymbolValue<zx_vaddr_t>(symbol_entries, "unknown").error_value());
}

TEST(StartArgsTest, ProgramValue) {
  fidl::Arena allocator;
  fidl::VectorView<fdata::wire::DictionaryEntry> program_entries(allocator, 2);
  program_entries[0].key.Set(allocator, "key-for-str");
  program_entries[0].value.set_str(allocator, "value-for-str");
  program_entries[1].key.Set(allocator, "key-for-strvec");
  program_entries[1].value.set_str_vec(allocator);
  fdata::wire::Dictionary program(allocator);
  program.set_entries(allocator, std::move(program_entries));

  EXPECT_EQ("value-for-str", *driver::ProgramValue(program, "key-for-str"));
  EXPECT_EQ(ZX_ERR_WRONG_TYPE, driver::ProgramValue(program, "key-for-strvec").error_value());
  EXPECT_EQ(ZX_ERR_NOT_FOUND, driver::ProgramValue(program, "key-unkown").error_value());

  fdata::wire::Dictionary empty_program;
  EXPECT_EQ(ZX_ERR_NOT_FOUND, driver::ProgramValue(empty_program, "").error_value());
}

TEST(StartArgsTest, NsValue) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  fidl::Arena allocator;
  fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(allocator, 1);
  ns_entries[0].Allocate(allocator);
  ns_entries[0].set_path(allocator, "/svc").set_directory(allocator, std::move(endpoints->client));

  auto svc = driver::NsValue(ns_entries, "/svc");
  zx_info_handle_basic_t client_info = {}, server_info = {};
  ASSERT_EQ(ZX_OK, svc->channel()->get_info(ZX_INFO_HANDLE_BASIC, &client_info, sizeof(client_info),
                                            nullptr, nullptr));
  ASSERT_EQ(ZX_OK, endpoints->server.channel().get_info(ZX_INFO_HANDLE_BASIC, &server_info,
                                                        sizeof(server_info), nullptr, nullptr));
  EXPECT_EQ(client_info.koid, server_info.related_koid);

  auto pkg = driver::NsValue(ns_entries, "/pkg");
  EXPECT_EQ(ZX_ERR_NOT_FOUND, pkg.error_value());
}
