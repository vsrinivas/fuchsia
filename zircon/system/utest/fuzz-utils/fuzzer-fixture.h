// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuzz-utils/string-map.h>
#include <stdint.h>

#include "fixture.h"

namespace fuzzing {
namespace testing {

// |fuzzing::testing::FuzzerFixture| is a fixture that understands fuzzer path locations.  It should
// not be instantiated directly; use |CreateZircon| or |CreateFuchsia| below.
class FuzzerFixture final : public Fixture {
public:
    // Creates a number of temporary, fake directories and files to mimic a deployment of
    // fuzz-packages on Fuchsia. The files and directories are automatically deleted when the
    // fixture is destroyed.
    bool Create() override;

    // Returns the maximum version of the given |Package| in the fixture as a C-style string, or
    // "0" if the package wasn't created by the fixture.
    const char* max_version(const char* package) const;

protected:
    // Resets the object to a pristine state.
    void Reset() override;

private:
    // Creates a fake fuzz |target| in the given |version| of a fake Fuchsia |package|. Adds fake
    // executable and data files as indicated by |flags|.
    bool CreatePackage(const char* package, long int version, const char* target);

    // Creates fake data mimicking outputs from a previous run of the fuzzer given by the |package|
    // and |target|.
    bool CreateData(const char* package, const char* target);

    // maps packages to maximum versions.
    StringMap max_versions_;
};

} // namespace testing
} // namespace fuzzing
