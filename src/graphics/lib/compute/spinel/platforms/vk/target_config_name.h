// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#undef  SPN_TARGET_VENDOR_ARCH
#undef  SPN_TARGET_CONFIG_NAME

#define SPN_TARGET_VENDOR_ARCH  CONCAT_MACRO(CONCAT_MACRO(SPN_TARGET_VENDOR,_),SPN_TARGET_ARCH)
#define SPN_TARGET_IMAGE_NAME   CONCAT_MACRO(spn_target_image_,SPN_TARGET_VENDOR_ARCH)

//
//
//

extern struct spn_target_image const SPN_TARGET_IMAGE_NAME;

//
//
//
