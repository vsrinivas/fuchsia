// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IES_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IES_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>

namespace wlan::nxpfmac {

// OUIs
constexpr uint8_t kOuiMicrosoft[] = {0x00, 0x50, 0xf2};

// OUI types
constexpr uint8_t kOuiTypeWmm = 0x02;

// Inspects each element to determine its length and ensures they all fit within `size`. Returns
// true if all elements and their payload fit within `ies` to `ies + size`, false otherwise.
bool is_valid_ie_range(const uint8_t* ies, size_t size);

// Represents an 802.11 information element. Note that ie points to the header of the information
// element.
class Ie {
 public:
  static constexpr size_t kTypeOffset = 0;
  static constexpr size_t kSizeOffset = 1;
  static constexpr size_t kHdrSize = 2;

  Ie() = default;
  explicit Ie(const uint8_t* ie) : ie_(ie) {}

  // Returns the type of the ie.
  uint8_t type() const { return ie_[kTypeOffset]; }
  // Returns the size of the payload.
  uint8_t size() const { return ie_[kSizeOffset]; }
  // Returns the size of the entire ie, including header.
  uint16_t raw_size() const { return size() + kHdrSize; }
  // Returns a pointer to the payload of the ie.
  const uint8_t* data() const { return ie_ + kHdrSize; }
  // Returns a pointer to the raw data of the ie, including header.
  const uint8_t* raw_data() const { return ie_; }

  bool is_vendor_specific_oui_type(const uint8_t (&oui)[3], uint8_t oui_type) const;

 private:
  const uint8_t* ie_ = nullptr;
};

class IeIterator {
 public:
  IeIterator() = default;
  explicit IeIterator(const Ie& ie) : ie_(ie) {}
  explicit IeIterator(const uint8_t* ie) : ie_(ie) {}

  const Ie& operator*() const { return ie_; }
  const Ie* operator->() const { return &ie_; }
  IeIterator& operator++() {
    const uint8_t* next_ie = ie_.data() + ie_.size();
    ie_ = Ie(next_ie);
    return *this;
  }
  bool operator!=(const IeIterator& other) const { return ie_.raw_data() != (*other).raw_data(); }
  bool operator==(const IeIterator& other) const { return ie_.raw_data() == (*other).raw_data(); }

 private:
  Ie ie_;
};

// A view of a sequence of information elements represented by a sequence of bytes. The view does
// not contain the data itself, it merely points to it and inspects it. Note that constructing an
// IeView will inspect each element's size initially to ensure the end of the list of IEs, incurring
// an O(n) cost to construction. The benefit is that for iteration the user does not have to ensure
// that the data is valid before constructing the IeView. The iterators will automatically stop once
// the end of the range is reached.
class IeView {
 public:
  // Construct a view of the IEs located at `ies`. `length` represented the total number of bytes
  // pointed to by `ies`. Note that if the elements inside `ies` extend beyond `length` they will
  // not be visible through the view.
  IeView(const uint8_t* ies, size_t length);

  IeIterator begin() const { return IeIterator(ies_); }
  IeIterator cbegin() const { return IeIterator(ies_); }
  IeIterator end() const { return IeIterator(ies_ + length_); }
  IeIterator cend() const { return IeIterator(ies_ + length_); }

  // Get a specific IE from the list of IEs. Returns an empty optional if it does not exist.
  std::optional<Ie> get(uint8_t ie_type) const;

  // Retrieve an IE cast to a specific type. Only returns a valid pointer if the IE exists and is
  // large enough to fit sizeof(T) bytes, returns nullptr otherwise. Note that this assumes that T
  // also contains the 2 bytes of type and length at the beginning of the IE.
  template <typename T>
  const T* get_as(uint8_t ie_type) const {
    std::optional<Ie> ie = get(ie_type);
    if (!ie.has_value()) {
      return nullptr;
    }

    if (ie->raw_size() < sizeof(T)) {
      return nullptr;
    }

    return reinterpret_cast<const T*>(ie->raw_data());
  }

 private:
  const uint8_t* const ies_;
  const size_t length_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IES_H_
