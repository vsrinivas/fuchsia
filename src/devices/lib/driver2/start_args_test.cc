// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/start_args.h"

#include <gtest/gtest.h>

namespace fdata = llcpp::fuchsia::data;
namespace fdf = llcpp::fuchsia::driver::framework;
namespace frunner = llcpp::fuchsia::component::runner;

TEST(StartArgsTest, Encode_Decode) {
  // Setup input.
  zx::channel node_client_end, node_server_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &node_client_end, &node_server_end));
  fdf::DriverSymbol symbol_entries[] = {
      fdf::DriverSymbol::Builder(std::make_unique<fdf::DriverSymbol::Frame>())
          .set_name(std::make_unique<fidl::StringView>("func"))
          .set_address(std::make_unique<zx_vaddr_t>(0xf00d))
          .build(),
  };
  auto symbols = fidl::unowned_vec(symbol_entries);
  fdata::DictionaryEntry program_entries[] = {
      {
          .key = "binary",
          .value = fdata::DictionaryValue::WithStr(std::make_unique<fidl::StringView>("path")),
      },
  };
  auto entries = fidl::unowned_vec(program_entries);
  auto program = fdata::Dictionary::Builder(std::make_unique<fdata::Dictionary::Frame>())
                     .set_entries(fidl::unowned_ptr(&entries))
                     .build();
  zx::channel svc_client_end, svc_server_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client_end, &svc_server_end));
  frunner::ComponentNamespaceEntry ns_entries[] = {
      frunner::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("svc"))
          .set_directory(std::make_unique<zx::channel>(std::move(svc_client_end)))
          .build(),
  };
  auto ns = fidl::unowned_vec(ns_entries);
  zx::channel outgoing_dir_client_end, outgoing_dir_server_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &outgoing_dir_client_end, &outgoing_dir_server_end));

  auto start_args = fdf::DriverStartArgs::UnownedBuilder()
                        .set_node(fidl::unowned_ptr(&node_client_end))
                        .set_symbols(fidl::unowned_ptr(&symbols))
                        .set_program(fidl::unowned_ptr(&program))
                        .set_ns(fidl::unowned_ptr(&ns))
                        .set_outgoing_dir(fidl::unowned_ptr(&outgoing_dir_server_end));

  // Encode
  auto storage = std::make_unique<start_args::Storage>();
  const char* error;
  auto encode_result = start_args::Encode(storage.get(), start_args.build(), &error);
  EXPECT_TRUE(encode_result.is_ok());

  // Decode
  auto decode_result = start_args::Decode(&encode_result.value(), &error);
  EXPECT_TRUE(decode_result.is_ok());

  // Verify output.
  auto start_args_ptr = decode_result.value();

  zx_info_handle_basic_t client_info = {}, server_info = {};
  ASSERT_EQ(ZX_OK, start_args_ptr->node().get_info(ZX_INFO_HANDLE_BASIC, &client_info,
                                                   sizeof(client_info), nullptr, nullptr));
  ASSERT_EQ(ZX_OK, node_server_end.get_info(ZX_INFO_HANDLE_BASIC, &server_info, sizeof(server_info),
                                            nullptr, nullptr));
  EXPECT_EQ(client_info.koid, server_info.related_koid);

  auto& decode_ns = start_args_ptr->ns();
  ASSERT_EQ(1u, decode_ns.count());
  EXPECT_STREQ("svc", decode_ns[0].path().data());
  ASSERT_EQ(ZX_OK, decode_ns[0].directory().get_info(ZX_INFO_HANDLE_BASIC, &client_info,
                                                     sizeof(client_info), nullptr, nullptr));
  ASSERT_EQ(ZX_OK, svc_server_end.get_info(ZX_INFO_HANDLE_BASIC, &server_info, sizeof(server_info),
                                           nullptr, nullptr));
  EXPECT_EQ(client_info.koid, server_info.related_koid);

  auto& decode_symbols = start_args_ptr->symbols();
  ASSERT_EQ(1u, decode_symbols.count());
  EXPECT_STREQ("func", decode_symbols[0].name().data());
  EXPECT_EQ(0xf00du, decode_symbols[0].address());

  auto& decode_entries = start_args_ptr->program().entries();
  ASSERT_EQ(1u, decode_entries.count());
  EXPECT_STREQ("binary", decode_entries[0].key.data());
  EXPECT_STREQ("path", decode_entries[0].value.str().data());

  ASSERT_EQ(ZX_OK, start_args_ptr->outgoing_dir().get_info(ZX_INFO_HANDLE_BASIC, &server_info,
                                                           sizeof(server_info), nullptr, nullptr));
  ASSERT_EQ(ZX_OK, outgoing_dir_client_end.get_info(ZX_INFO_HANDLE_BASIC, &client_info,
                                                    sizeof(client_info), nullptr, nullptr));
  EXPECT_EQ(client_info.koid, server_info.related_koid);
}

TEST(StartArgsTest, SymbolValue) {
  fdf::DriverSymbol symbol_entries[] = {
      fdf::DriverSymbol::Builder(std::make_unique<fdf::DriverSymbol::Frame>())
          .set_name(std::make_unique<fidl::StringView>("sym"))
          .set_address(std::make_unique<zx_vaddr_t>(0xfeed))
          .build(),
  };
  auto symbols = fidl::unowned_vec(symbol_entries);

  EXPECT_EQ(0xfeedu, start_args::symbol_value<zx_vaddr_t>(symbols, "sym").value());
  EXPECT_EQ(ZX_ERR_NOT_FOUND,
            start_args::symbol_value<zx_vaddr_t>(symbols, "unknown").error_value());
}

TEST(StartArgsTest, ProgramValue) {
  fidl::StringView str = "value-for-str";
  fidl::VectorView<fidl::StringView> strvec;
  fdata::DictionaryEntry program_entries[] = {
      {
          .key = "key-for-str",
          .value = fdata::DictionaryValue::WithStr(fidl::unowned_ptr(&str)),
      },
      {
          .key = "key-for-strvec",
          .value = fdata::DictionaryValue::WithStrVec(fidl::unowned_ptr(&strvec)),
      },
  };
  auto entries = fidl::unowned_vec(program_entries);
  auto program = fdata::Dictionary::Builder(std::make_unique<fdata::Dictionary::Frame>())
                     .set_entries(fidl::unowned_ptr(&entries))
                     .build();

  EXPECT_EQ("value-for-str", start_args::program_value(program, "key-for-str").value());
  EXPECT_EQ(ZX_ERR_WRONG_TYPE, start_args::program_value(program, "key-for-strvec").error_value());
  EXPECT_EQ(ZX_ERR_NOT_FOUND, start_args::program_value(program, "key-unkown").error_value());

  fdata::Dictionary empty_program;
  EXPECT_EQ(ZX_ERR_NOT_FOUND, start_args::program_value(empty_program, "").error_value());
}

TEST(StartArgsTest, NsValue) {
  zx::channel client_end, server_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &client_end, &server_end));
  frunner::ComponentNamespaceEntry ns_entries[] = {
      frunner::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("/svc"))
          .set_directory(std::make_unique<zx::channel>(std::move(client_end)))
          .build(),
  };
  auto entries = fidl::unowned_vec(ns_entries);

  auto svc = start_args::ns_value(entries, "/svc");
  zx_info_handle_basic_t client_info = {}, server_info = {};
  ASSERT_EQ(ZX_OK, svc.value()->get_info(ZX_INFO_HANDLE_BASIC, &client_info, sizeof(client_info),
                                         nullptr, nullptr));
  ASSERT_EQ(ZX_OK, server_end.get_info(ZX_INFO_HANDLE_BASIC, &server_info, sizeof(server_info),
                                       nullptr, nullptr));
  EXPECT_EQ(client_info.koid, server_info.related_koid);

  auto pkg = start_args::ns_value(entries, "/pkg");
  EXPECT_EQ(ZX_ERR_NOT_FOUND, pkg.error_value());
}
