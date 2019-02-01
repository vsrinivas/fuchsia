// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
// clang-format is off because it will take these lists and turn them into paragraphs.

const char futility_version[] = "v0.0.fuchsia";
#define _CMD(NAME) extern const struct futil_cmd_t __cmd_##NAME;
_CMD(bdb)
_CMD(create)
_CMD(dump_fmap)
_CMD(gbb)
_CMD(gbb_utility)
_CMD(help)
_CMD(load_fmap)
_CMD(pcr)
_CMD(show)
_CMD(sign)
_CMD(validate_rec_mrc)
_CMD(vbutil_firmware)
_CMD(vbutil_kernel)
_CMD(vbutil_key)
_CMD(vbutil_keyblock)
_CMD(verify)
_CMD(version)
#undef _CMD
#define _CMD(NAME) &__cmd_##NAME,
const struct futil_cmd_t *const futil_cmds[] = {
_CMD(bdb)
_CMD(create)
_CMD(dump_fmap)
_CMD(gbb)
_CMD(gbb_utility)
_CMD(help)
_CMD(load_fmap)
_CMD(pcr)
_CMD(show)
_CMD(sign)
_CMD(validate_rec_mrc)
_CMD(vbutil_firmware)
_CMD(vbutil_kernel)
_CMD(vbutil_key)
_CMD(vbutil_keyblock)
_CMD(verify)
_CMD(version)
0};  /* null-terminated */
#undef _CMD
