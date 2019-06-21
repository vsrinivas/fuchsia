// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <usb/usb.h>

namespace usb {

// InterfaceList implementation

InterfaceList::InterfaceList(const ddk::UsbProtocolClient& client, bool skip_alt)
    : skip_alt_(skip_alt) {
    usb_protocol_t proto;
    client.GetProto(&proto);
    iter_ = {};
    status_ = usb_desc_iter_init(&proto, &iter_);
}

zx_status_t InterfaceList::check() const {
    return status_;
}

InterfaceList::~InterfaceList() {
    if (iter_.desc) {
        usb_desc_iter_release(&iter_);
    }
}

InterfaceList::iterator_impl::iterator_impl(const usb_desc_iter_t& iter, bool skip_alt)
    : skip_alt_(skip_alt) {
    if (!iter.desc) {
        const usb_desc_iter_t iter = {};
        interface_ = Interface(iter);
        return;
    }
    interface_ = Interface(iter);
    interface_.Next(skip_alt_);
}

InterfaceList::const_iterator InterfaceList::begin() const {
    if (!iter_.desc) {
        return end();
    }
    return const_iterator(iter_, skip_alt_);
}

const InterfaceList::const_iterator InterfaceList::cbegin() const {
    if (!iter_.desc) {
        return end();
    }
    return const_iterator(iter_, skip_alt_);
}

InterfaceList::const_iterator InterfaceList::end() const {
    usb_desc_iter_t init = {};
    return const_iterator(init, skip_alt_);
}

const InterfaceList::const_iterator InterfaceList::cend() const {
    usb_desc_iter_t init = {};
    return const_iterator(init, skip_alt_);
}

// InterfaceList::iterator_impl implementation

InterfaceList::iterator_impl InterfaceList::iterator_impl::operator++(int) {
    iterator_impl ret(*this);
    ++(*this);
    return ret;
}

InterfaceList::iterator_impl& InterfaceList::iterator_impl::operator++() {
    interface_.Next(skip_alt_);
    return *this;
}

// Interface implementation

void Interface::Next(bool skip_alt) {
    descriptor_ = usb_desc_iter_next_interface(&iter_, skip_alt);
}

Interface::const_iterator Interface::begin() const {
    if (!iter_.desc) {
        return end();
    }
    return const_iterator(iter_);
}

const Interface::const_iterator Interface::cbegin() const {
    if (!iter_.desc) {
        return cend();
    }
    return const_iterator(iter_);
}

Interface::const_iterator Interface::end() const {
    usb_desc_iter_t init = {};
    return const_iterator(init);
}

const Interface::const_iterator Interface::cend() const {
    usb_desc_iter_t init = {};
    return const_iterator(init);
}

// Interface::iterator_impl implementation

Interface::iterator_impl Interface::iterator_impl::operator++(int) {
    iterator_impl ret(*this);
    ++(*this);
    return ret;
}

Interface::iterator_impl& Interface::iterator_impl::operator++() {
    endpoint_ = usb_desc_iter_next_endpoint(&iter_);
    return *this;
}

Interface::iterator_impl::iterator_impl(const usb_desc_iter_t& iter)
    : iter_(iter) {
    if (!iter.desc) {
        endpoint_ = nullptr;
        return;
    }
    endpoint_ = usb_desc_iter_next_endpoint(&iter_);
}

} // namespace usb
