// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_TESTS_HELPER_CONFIG_NAMESPACE_HELPER_H_
#define GARNET_LIB_MAGMA_TESTS_HELPER_CONFIG_NAMESPACE_HELPER_H_

// This installs the vulkan configuration directory "/system/data/vulkan" into
// this process' global namespace at "/config/vulkan" to match the namespace
// created by components created with the "vulkan" component feature.
//
// TODO(CP-77): Once this issue is resolved, these tests can just run normally.
bool InstallConfigDirectoryIntoGlobalNamespace();

#endif // GARNET_LIB_MAGMA_TESTS_HELPER_CONFIG_NAMESPACE_HELPER_H_
