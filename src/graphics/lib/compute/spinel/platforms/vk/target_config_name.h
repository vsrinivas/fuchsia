// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TARGET_CONFIG_NAME_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TARGET_CONFIG_NAME_H_

//
// clang-format off
//

#undef  SPN_TARGET_VENDOR_ARCH
#undef  SPN_TARGET_CONFIG_NAME

#define SPN_TARGET_VENDOR_ARCH  CONCAT_MACRO(CONCAT_MACRO(SPN_TARGET_VENDOR,_),SPN_TARGET_ARCH)
#define SPN_TARGET_IMAGE_NAME   CONCAT_MACRO(spn_target_image_,SPN_TARGET_VENDOR_ARCH)

//
// clang-format on
//

extern struct spn_target_image const SPN_TARGET_IMAGE_NAME;

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TARGET_CONFIG_NAME_H_
