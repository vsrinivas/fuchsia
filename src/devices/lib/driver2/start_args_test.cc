// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/start_args.h"

#include <gtest/gtest.h>

namespace fdata = llcpp::fuchsia::data;
namespace fdf = llcpp::fuchsia::driver::framework;
namespace frunner = llcpp::fuchsia::component::runner;

TEST(StartArgsTest, SymbolValue) {
  fdf::NodeSymbol symbol_entries[] = {
      fdf::NodeSymbol::Builder(std::make_unique<fdf::NodeSymbol::Frame>())
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
  auto endpoints = fidl::CreateEndpoints<llcpp::fuchsia::io::Directory>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  frunner::ComponentNamespaceEntry ns_entries[] = {
      frunner::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("/svc"))
          .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>(
              std::move(endpoints->client)))
          .build(),
  };
  auto entries = fidl::unowned_vec(ns_entries);

  auto svc = start_args::ns_value(entries, "/svc");
  zx_info_handle_basic_t client_info = {}, server_info = {};
  ASSERT_EQ(ZX_OK, zx_object_get_info(svc.value().channel(), ZX_INFO_HANDLE_BASIC, &client_info,
                                      sizeof(client_info), nullptr, nullptr));
  ASSERT_EQ(ZX_OK, endpoints->server.channel().get_info(ZX_INFO_HANDLE_BASIC, &server_info,
                                                        sizeof(server_info), nullptr, nullptr));
  EXPECT_EQ(client_info.koid, server_info.related_koid);

  auto pkg = start_args::ns_value(entries, "/pkg");
  EXPECT_EQ(ZX_ERR_NOT_FOUND, pkg.error_value());
}
