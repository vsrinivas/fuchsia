// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launch_app.h"

#include <src/lib/fxl/strings/string_printf.h>

#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace netemul {
namespace config {

static const char* kUrl = "url";
static const char* kArguments = "arguments";
static const char* kEmptyUrl = "";

bool LaunchApp::ParseFromJSON(const rapidjson::Value& value,
                              json::JSONParser* parser) {
  if (value.IsString()) {
    // value is a string, parse as url only
    auto url = value.GetString();
    if (value.GetStringLength() != 0) {
      component::FuchsiaPkgUrl pkgUrl;
      if (!pkgUrl.Parse(url)) {
        parser->ReportError(
            "launch options url is not a valid fuchsia package url");
        return false;
      }
    }
    url_ = url;
    arguments_.clear();

  } else if (value.IsObject()) {
    // set defaults:
    url_ = kEmptyUrl;
    arguments_.clear();

    // iterate over members:
    for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
      if (i->name == kUrl) {
        if (!i->value.IsString()) {
          parser->ReportError("launch options url must be string");
          return false;
        }
        auto v = i->value.GetString();
        if (i->value.GetStringLength() != 0) {
          component::FuchsiaPkgUrl pkgUrl;
          if (!pkgUrl.Parse(v)) {
            parser->ReportError(
                "launch options url is not a valid fuchsia package url");
            return false;
          }
        }
        url_ = v;
      } else if (i->name == kArguments) {
        if (!i->value.IsArray()) {
          parser->ReportError(
              "launch options arguments must be array of string");
          return false;
        }
        auto arg_arr = i->value.GetArray();
        for (auto a = arg_arr.Begin(); a != arg_arr.End(); a++) {
          if (!a->IsString()) {
            parser->ReportError(
                "launch options arguments element must be string");
            return false;
          }
          arguments_.emplace_back(a->GetString());
        }
      } else {
        parser->ReportError(fxl::StringPrintf(
            "Unrecognized launch args member \"%s\"", i->name.GetString()));
        return false;
      }
    }
  } else {
    parser->ReportError("launch options must be of type object or string");
    return false;
  }

  return true;
}

const std::string& LaunchApp::url() const { return url_; }

const std::vector<std::string>& LaunchApp::arguments() const {
  return arguments_;
}

const std::string& LaunchApp::GetUrlOrDefault(const std::string& def) const {
  if (url_.empty()) {
    return def;
  } else {
    return url_;
  }
}

}  // namespace config
}  // namespace netemul
