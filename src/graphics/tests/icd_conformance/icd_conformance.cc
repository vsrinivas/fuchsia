// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.vulkan.loader/cpp/wire.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fit/defer.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/service/llcpp/service.h>

#include <set>

#include <gtest/gtest.h>

#include "rapidjson/prettywriter.h"
#include "rapidjson/schema.h"
#include "src/lib/elflib/elflib.h"
#include "src/lib/files/directory.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/json_parser/pretty_print.h"

namespace {
const char* kManifestFsPath = "/manifestfs";

const char* kManifestSchema = R"(
{
  "$schema":"http://json-schema.org/schema#",
  "type":"object",
  "properties":{
    "file_format_version":{
      "type":"string"
    },
    "ICD":{
      "type":"object",
      "properties":{
        "library_path":{
          "type":"string"
        },
        "api_version":{
          "type":"string"
        }
      },
      "required":[
        "library_path",
        "api_version"
      ]
    }
  },
  "required":[
    "file_format_version",
    "ICD"
  ]
}
)";

void ValidateSharedObject(const zx::vmo& vmo) {
  fzl::VmoMapper mapper;
  ASSERT_EQ(ZX_OK, mapper.Map(vmo, 0, 0, ZX_VM_PERM_READ));

  std::unique_ptr<elflib::ElfLib> lib =
      elflib::ElfLib::Create(reinterpret_cast<uint8_t*>(mapper.start()), mapper.size());
  ASSERT_TRUE(lib);

  auto deps = lib->GetSharedObjectDependencies();

  ASSERT_TRUE(deps);
  const std::set<std::string> kSoAllowlist{"libzircon.so", "libc.so"};

  // Validate all needed shared libraries against allowlist.
  for (const std::string& dep : *deps) {
    EXPECT_TRUE(kSoAllowlist.count(dep)) << "Disallowed library: " << dep;
  }
  EXPECT_LT(0u, deps->size());

  auto warnings = lib->GetAndClearWarnings();

  for (const std::string& warning : warnings) {
    ADD_FAILURE() << warning;
  }
}

bool ValidateManifestJson(const rapidjson::GenericDocument<rapidjson::UTF8<>>& doc) {
  rapidjson::Document schema_doc;
  schema_doc.Parse(kManifestSchema);
  EXPECT_FALSE(schema_doc.HasParseError()) << schema_doc.GetParseError();

  rapidjson::SchemaDocument schema(schema_doc);
  rapidjson::SchemaValidator validator(schema);
  if (!doc.Accept(validator)) {
    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
    validator.GetError().Accept(w);
    ADD_FAILURE() << "manifest.json failed validation " << sb.GetString();
    return false;
  }
  return true;
}

void ValidateIcd(fidl::WireSyncClient<fuchsia_vulkan_loader::Loader>& loader,
                 const std::string& manifest_filename) {
  json_parser::JSONParser manifest_parser;
  auto manifest_doc =
      manifest_parser.ParseFromFile(std::string(kManifestFsPath) + "/" + manifest_filename);

  ASSERT_FALSE(manifest_parser.HasError()) << manifest_parser.error_str();

  ASSERT_TRUE(ValidateManifestJson(manifest_doc));

  std::string library_path = manifest_doc["ICD"].GetObject()["library_path"].GetString();

  auto res = loader->Get(fidl::StringView::FromExternal(library_path));
  ASSERT_EQ(res.status(), ZX_OK);
  zx::vmo vmo = std::move(res->lib);
  ValidateSharedObject(vmo);
}

TEST(IcdConformance, SharedLibraries) {
  auto svc = service::OpenServiceRoot();
  auto client_end = service::ConnectAt<fuchsia_vulkan_loader::Loader>(*svc);

  auto client = fidl::BindSyncClient(std::move(*client_end));

  auto manifest_fs_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(ZX_OK, manifest_fs_endpoints.status_value());
  auto manifest_result =
      client->ConnectToManifestFs(fuchsia_vulkan_loader::ConnectToManifestOptions::kWaitForIdle,
                                  manifest_fs_endpoints->server.TakeChannel());
  EXPECT_EQ(ZX_OK, manifest_result.status());

  fdio_ns_t* ns;
  EXPECT_EQ(ZX_OK, fdio_ns_get_installed(&ns));
  EXPECT_EQ(ZX_OK, fdio_ns_bind(ns, kManifestFsPath,
                                manifest_fs_endpoints->client.TakeChannel().release()));
  auto defer_unbind = fit::defer([&]() { fdio_ns_unbind(ns, kManifestFsPath); });

  std::vector<std::string> manifests;
  EXPECT_TRUE(files::ReadDirContents(kManifestFsPath, &manifests));

  for (auto& manifest : manifests) {
    if (manifest == ".")
      continue;
    SCOPED_TRACE(manifest);
    ValidateIcd(client, manifest);
  }
}
}  // namespace
