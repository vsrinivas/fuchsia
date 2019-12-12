// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_MOCK_SPINEL_TEST_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_MOCK_SPINEL_TEST_UTILS_H_

#include <gtest/gtest.h>

#include <ostream>

#include "mock_spinel.h"

namespace mock_spinel {

// Convenience testing class for Spinel.
// This registers a new MockSpinel() implementation on SetUp(), and
// removes it on TearDown(). It also allows you to retrieve the mock
// Spinel context with mock_context().
class Test : public ::testing::Test {
 private:
  static spinel_api::Interface * previous_interface_;
  static Spinel *                spinel_;

 protected:
  // Global test suite initialization/finalization, used to register the
  // SpinelTest implementation globally.
  static void
  SetUpTestSuite()
  {
    spinel_             = new Spinel();
    previous_interface_ = spinel_api::SetImplementation(spinel_);
  }

  static void
  TearDownTestSuite()
  {
    spinel_api::SetImplementation(previous_interface_);
    previous_interface_ = nullptr;
    delete spinel_;
    spinel_ = nullptr;
  }

  // Test fixture setup creates a set of related Spinel objects.
  void
  SetUp() override
  {
    spinel_->createContext(&context_);
    spn_path_builder_create(context_, &path_builder_);
    spn_raster_builder_create(context_, &raster_builder_);
    spn_composition_create(context_, &composition_);
    spn_styling_create(context_, &styling_, 16, 16);
  }

  // Test fixture tear down releases the Spinel objects.
  void
  TearDown() override
  {
    spn_styling_release(styling_);
    spn_composition_release(composition_);
    spn_raster_builder_release(raster_builder_);
    spn_path_builder_release(path_builder_);
    spn_context_release(context_);
  }

  // Accessors to the mock spinel wrapper classes.
  const Context *
  mock_context() const
  {
    return Context::fromSpinel(context_);
  }
  Context *
  mock_context()
  {
    return Context::fromSpinel(context_);
  }
  const PathBuilder *
  mock_path_builder() const
  {
    return PathBuilder::fromSpinel(path_builder_);
  }
  PathBuilder *
  mock_path_builder()
  {
    return PathBuilder::fromSpinel(path_builder_);
  }
  const RasterBuilder *
  mock_raster_builder() const
  {
    return RasterBuilder::fromSpinel(raster_builder_);
  }
  RasterBuilder *
  mock_raster_builder()
  {
    return RasterBuilder::fromSpinel(raster_builder_);
  }
  const Composition *
  mock_composition() const
  {
    return Composition::fromSpinel(composition_);
  }
  Composition *
  mock_composition()
  {
    return Composition::fromSpinel(composition_);
  }
  const Styling *
  mock_styling() const
  {
    return Styling::fromSpinel(styling_);
  }
  Styling *
  mock_styling()
  {
    return Styling::fromSpinel(styling_);
  }

  spn_context_t        context_;
  spn_path_builder_t   path_builder_;
  spn_raster_builder_t raster_builder_;
  spn_composition_t    composition_;
  spn_styling_t        styling_;
};

// GoogleTest-compatible printers for mock_spinel data types.
extern std::ostream &
operator<<(std::ostream & os, const Path & path);
extern std::ostream &
operator<<(std::ostream & os, const RasterPath & raster_path);
extern std::ostream &
operator<<(std::ostream & os, const Raster & raster);
extern std::ostream &
operator<<(std::ostream & os, const Composition & composition);
extern std::ostream &
operator<<(std::ostream & os, const StylingGroup & styling_group);
extern std::ostream &
operator<<(std::ostream & os, const Styling & styling);

}  // namespace mock_spinel

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_MOCK_SPINEL_TEST_UTILS_H_
