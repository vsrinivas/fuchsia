// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting.h"

#include <algorithm>
#include <vector>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace zxdb {


Setting::Setting() = default;

Setting::Setting(bool val) : type_(Setting::Type::kBoolean) {
  values_ = VariantValue(std::in_place_index_t<0>{}, val);
}

Setting::Setting(int val) : type_(Setting::Type::kInteger) {
  values_ = VariantValue(std::in_place_index_t<1>{}, val);
}

Setting::Setting(const char* val) : type_(Setting::Type::kString) {
  values_ = VariantValue(std::in_place_index_t<2>{}, val);
}

Setting::Setting(std::string val) : type_(Setting::Type::kString) {
  values_ = VariantValue(std::in_place_index_t<2>{}, std::move(val));
}

Setting::Setting(std::vector<std::string> val)
    : type_(Setting::Type::kStringList) {
  values_ = VariantValue(std::in_place_index_t<3>{}, std::move(val));
}

bool& Setting::GetBool() {
  FXL_DCHECK(type_ == Setting::Type::kBoolean);
  return std::get<bool>(values_);
}

bool Setting::GetBool() const { return const_cast<Setting*>(this)->GetBool(); }

int& Setting::GetInt() {
  FXL_DCHECK(type_ == Setting::Type::kInteger);
  return std::get<int>(values_);
}

int Setting::GetInt() const { return const_cast<Setting*>(this)->GetInt(); }

std::string& Setting::GetString() {
  FXL_DCHECK(type_ == Setting::Type::kString);
  return std::get<std::string>(values_);
}

const std::string& Setting::GetString() const {
  FXL_DCHECK(type_ == Setting::Type::kString);
  return std::get<std::string>(values_);
}

std::vector<std::string>& Setting::GetStringList() {
  FXL_DCHECK(type_ == Setting::Type::kStringList);
  return std::get<std::vector<std::string>>(values_);
}

const std::vector<std::string>& Setting::GetStringList() const {
  FXL_DCHECK(type_ == Setting::Type::kStringList);
  return std::get<std::vector<std::string>>(values_);
}

}  // namespace zxdb
