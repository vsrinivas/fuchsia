// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace netemul {
namespace config {

static const char* kLabel = "label";
static const char* kUrl = "url";
static const char* kFiles = "files";
static const char* kMacAddrs = "macs";

bool Guest::ParseFromJSON(const rapidjson::Value& value, json::JSONParser* parser) {
  if (!value.IsObject()) {
    parser->ReportError("guest must be object type");
    return false;
  }

  // Set default vaules.
  guest_image_url_ = "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx";
  guest_label_ = "debian_guest";
  files_.clear();
  macs_.clear();

  for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
    if (i->name == kLabel) {
      if ((!i->value.IsString()) || i->value.GetStringLength() == 0) {
        parser->ReportError("guest label must be a non-empty string");
        return false;
      }
      guest_label_ = i->value.GetString();
    } else if (i->name == kUrl) {
      if ((!i->value.IsString()) || i->value.GetStringLength() == 0) {
        parser->ReportError("guest URL must be a non-empty string");
        return false;
      }
      guest_image_url_ = i->value.GetString();
    } else if (i->name == kFiles) {
      if (!i->value.IsObject()) {
        parser->ReportError(
            "guest files must be an object mapping local source to guest VM destination");
        return false;
      }
      for (auto f = i->value.MemberBegin(); f != i->value.MemberEnd(); f++) {
        files_[f->name.GetString()] = f->value.GetString();
      }
    } else if (i->name == kMacAddrs) {
      if (!i->value.IsObject()) {
        parser->ReportError("guest macs must be an object mapping MAC to ethertap network");
        return false;
      }
      for (auto f = i->value.MemberBegin(); f != i->value.MemberEnd(); f++) {
        if (!f->value.IsString()) {
          parser->ReportError("guest macs must map to strings representing ethertap networks");
          return false;
        }
        macs_[f->name.GetString()] = f->value.GetString();
      }
    } else {
      parser->ReportError(
          fxl::StringPrintf("Unrecognized guest member \"%s\"", i->name.GetString()));
      return false;
    }
  }

  return true;
}

const std::string& Guest::guest_image_url() const { return guest_image_url_; };

const std::string& Guest::guest_label() const { return guest_label_; };

const std::map<std::string, std::string>& Guest::files() const { return files_; };

const std::map<std::string, std::string>& Guest::macs() const { return macs_; };

}  // namespace config
}  // namespace netemul
