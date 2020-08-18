// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class ErrInjTest : public SimTest {
 public:
  void RunCountryTest(const std::vector<uint8_t>& input,
                      const std::array<uint8_t, 2>& expected_output);
};

void ErrInjTest::RunCountryTest(const std::vector<uint8_t>& input,
                                const std::array<uint8_t, 2>& expected_output) {
  // Allocate our alternative injection data. It will be mapped to a brcmf_fil_country_le struct
  // by the driver. We will provide enough data to at least reach the start of the "ccode" field
  // of the structure. Anything beyond that is provided by the individual test.
  constexpr off_t ccode_offset = offsetof(brcmf_fil_country_le, ccode);
  size_t inj_data_size = ccode_offset + input.size();
  std::vector<uint8_t> alt_cc_data(inj_data_size, 0);
  for (size_t ndx = 0; ndx < input.size(); ndx++) {
    alt_cc_data[ccode_offset + ndx] = input[ndx];
  }

  // Set up our injector
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("country", ZX_OK, std::nullopt, &alt_cc_data);

  // Get the results and verify that the country code matches the first two characters of our input
  wlanphy_country_t actual_cc;
  device_->WlanphyImplGetCountry(&actual_cc);
  EXPECT_EQ(actual_cc.alpha2[0], expected_output[0]);
  EXPECT_EQ(actual_cc.alpha2[1], expected_output[1]);
}

TEST_F(ErrInjTest, ErrInjectorReplacementValues) {
  ASSERT_EQ(Init(), ZX_OK);

  // Less data than needed - the rest should be filled with zeroes
  RunCountryTest({}, {0, 0});
  RunCountryTest({'A'}, {'A', 0});

  // Just enough data to fill the parts of the output we care about
  RunCountryTest({'A', 'B'}, {'A', 'B'});

  // More data than the structure can contain -- this is OK, the injector should only use as much
  // as it needs.
  RunCountryTest({'A', 'B', 'C', 'D', 'E'}, {'A', 'B'});
}

}  // namespace wlan::brcmfmac
