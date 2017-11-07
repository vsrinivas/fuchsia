// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_CATEGORIES_H_
#define GARNET_BIN_CPUPERF_PROVIDER_CATEGORIES_H_

#include <stddef.h>
#include <stdint.h>

namespace cpuperf_provider {

struct IpmCategory {
    const char *name;
    uint32_t value;
};

size_t GetNumCategories();

const IpmCategory& GetCategory(size_t cat);

const IpmCategory& GetProgrammableCategoryFromId(uint32_t id);

uint64_t GetSampleFreq(uint32_t category_mask);

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_CATEGORIES_H_
