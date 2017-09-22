// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>
#include <map>

#include "peridot/lib/fidl/json_xdr.h"
#include "gtest/gtest.h"

namespace modular {
namespace {

struct T {
  int i;
  std::string s;

  std::vector<int> vi;
  std::vector<std::string> vs;

  std::map<int, int> mi;
  std::map<std::string, int> ms;
};

void XdrT(XdrContext* const xdr, T* const data) {
  xdr->Field("i", &data->i);
  xdr->Field("s", &data->s);
  xdr->Field("vi", &data->vi);
  xdr->Field("vs", &data->vs);
  xdr->Field("mi", &data->mi);
  xdr->Field("ms", &data->ms);
}

TEST(Xdr, Struct) {
  T t;
  std::string json;
  XdrWrite(&json, &t, XdrT);
  EXPECT_TRUE(XdrRead(json, &t, XdrT));
}

}  // namespace
}  // namespace modular
