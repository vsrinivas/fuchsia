// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// Reads Linux device tree to initialize command line and return ramdisk location
void read_device_tree(void** ramdisk_base, size_t* ramdisk_size, size_t* mem_size);

// reserves memory for the ramdisk so the kernel won't step on it
void platform_preserve_ramdisk(void);
