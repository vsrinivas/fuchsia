#!/bin/sh

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

if [ $# -ne 1 ]
then
  echo "Usage: do-update.sh /path/to/chromium/base/numerics"
  exit 65
fi

# Copy in pristine version.
cp $1/* .

# Update to local maintainer(s).
printf 'scottmg@google.com\n*\n' > OWNERS

# Remove Chromium-specific file.
rm -f DEPS

# Replace header guards and some macros.
sed -i -e 's/BASE_NUMERICS_/SAFEMATH_/g' *.h
sed -i -e 's/BASE_/SAFEMATH_/g' *.h

# Update include paths.
sed -i -e 's/#include "base\/numerics\/\(.*\)"/#include <safemath\/\1>/g' *.h

# TODO(TC-383): This is some hackery because cmath isn't includable with the
# current Fuchsia toolchain, so make the library usable with musl's math.h
# instead.
sed -i -e 's/#include <cmath>/#include <math.h>/g' *.h
sed -i -e 's/std::isfinite(/isfinite(/g' *.h

# Update to local namespace.
sed -i -e 's/namespace base/namespace safemath/g' *.h
sed -i -e 's/base::/safemath::/g' *.h

# Update .md documentation.
sed -i -e 's/base\/numerics/safemath/g' *.md

# Write a BUILD.gn.
(
cat <<END
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

library("safemath") {
  sdk = "source"
  sdk_headers = [
    "safemath/checked_math.h",
    "safemath/checked_math_impl.h",
    "safemath/clamped_math.h",
    "safemath/clamped_math_impl.h",
    "safemath/math_constants.h",
    "safemath/ranges.h",
    "safemath/safe_conversions.h",
    "safemath/safe_conversions_arm_impl.h",
    "safemath/safe_conversions_impl.h",
    "safemath/safe_math.h",
    "safemath/safe_math_arm_impl.h",
    "safemath/safe_math_clang_gcc_impl.h",
    "safemath/safe_math_shared_impl.h",
  ]
  sources = []
  host = true
  kernel = false
  static = true
}
END
) >BUILD.gn

# Reformat due to different line lengths, but don't reformat to local style to
# keep diff small.
../../../prebuilt/downloads/clang/bin/clang-format -i -style=Chromium *.h

rm -rf include/safemath
mkdir -p include/safemath
mv *.h include/safemath/
