#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/test_utils.h"

namespace wlan {
namespace brcmfmac {

bool ConcatenatedEquals(const std::initializer_list<std::string_view>& lhs,
                        const std::initializer_list<std::string_view>& rhs) {
  size_t lhs_size = 0;
  for (const auto& s : lhs) {
    lhs_size += s.size();
  }
  size_t rhs_size = 0;
  for (const auto& s : rhs) {
    rhs_size += s.size();
  }
  EXPECT_EQ(lhs_size, rhs_size);
  if (lhs_size != rhs_size) {
    return false;
  }

  auto lhs_iter = lhs.begin();
  auto rhs_iter = rhs.begin();
  if (lhs_iter != lhs.end() && rhs_iter != rhs.end()) {
    auto l_iter = lhs_iter->cbegin();
    auto r_iter = rhs_iter->cbegin();
    while (true) {
      if (*l_iter != *r_iter) {
        return false;
      }
      ++l_iter;
      if (l_iter == lhs_iter->cend()) {
        ++lhs_iter;
        if (lhs_iter != lhs.end()) {
          l_iter = lhs_iter->cbegin();
        }
      }
      ++r_iter;
      if (r_iter == rhs_iter->cend()) {
        ++rhs_iter;
        if (rhs_iter != rhs.end()) {
          r_iter = rhs_iter->cbegin();
        }
      }
      if (lhs_iter == lhs.end() || rhs_iter == rhs.end()) {
        break;
      }
    }
  }

  EXPECT_EQ(lhs_iter, lhs.end());
  EXPECT_EQ(rhs_iter, rhs.end());
  return (lhs_iter == lhs.end() && rhs_iter == rhs.end());
}

}  // namespace brcmfmac
}  // namespace wlan
