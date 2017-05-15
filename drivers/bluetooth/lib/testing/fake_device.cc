// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_device.h"

#include "apps/bluetooth/lib/hci/event_packet.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/random/rand.h"

namespace bluetooth {
namespace testing {
namespace {

void WriteRandomRSSI(int8_t* out_mem) {
  constexpr int8_t kRSSIMin = -127;
  constexpr int8_t kRSSIMax = 20;

  int8_t rssi;
  ftl::RandBytes(reinterpret_cast<unsigned char*>(&rssi), sizeof(rssi));
  rssi = (rssi % (kRSSIMax - kRSSIMin)) + kRSSIMin;

  *out_mem = rssi;
}

}  // namespace

FakeDevice::FakeDevice(const common::DeviceAddress& address, bool connectable, bool scannable)
    : address_(address),
      connectable_(connectable),
      scannable_(scannable),
      should_batch_reports_(false) {}

void FakeDevice::SetAdvertisingData(const common::ByteBuffer& data) {
  FTL_DCHECK(data.GetSize() <= hci::kMaxLEAdvertisingDataLength);
  adv_data_ = common::DynamicByteBuffer(data);
}

void FakeDevice::SetScanResponse(bool should_batch_reports, const common::ByteBuffer& data) {
  FTL_DCHECK(scannable_);
  FTL_DCHECK(data.GetSize() <= hci::kMaxLEAdvertisingDataLength);
  scan_rsp_ = common::DynamicByteBuffer(data);
  should_batch_reports_ = should_batch_reports;
}

common::DynamicByteBuffer FakeDevice::CreateAdvertisingReportEvent(bool include_scan_rsp) const {
  size_t event_size = hci::EventPacket::GetMinBufferSize(
      sizeof(hci::LEMetaEventParams) + sizeof(hci::LEAdvertisingReportSubeventParams) +
      sizeof(hci::LEAdvertisingReportData) + adv_data_.GetSize() + sizeof(int8_t));
  if (include_scan_rsp) {
    FTL_DCHECK(scannable_);
    event_size += sizeof(hci::LEAdvertisingReportData) + scan_rsp_.GetSize() + sizeof(int8_t);
  }

  common::DynamicByteBuffer buffer(event_size);
  hci::MutableEventPacket event_packet(hci::kLEMetaEventCode, &buffer);
  auto payload = event_packet.GetMutablePayload<hci::LEMetaEventParams>();
  payload->subevent_code = hci::kLEAdvertisingReportSubeventCode;

  auto subevent_payload =
      reinterpret_cast<hci::LEAdvertisingReportSubeventParams*>(payload->subevent_parameters);
  subevent_payload->num_reports = include_scan_rsp ? 2 : 1;

  auto report = reinterpret_cast<hci::LEAdvertisingReportData*>(subevent_payload->reports);
  if (connectable_) {
    report->event_type = hci::LEAdvertisingEventType::kAdvInd;
  } else if (scannable_) {
    report->event_type = hci::LEAdvertisingEventType::kAdvScanInd;
  } else {
    report->event_type = hci::LEAdvertisingEventType::kAdvNonConnInd;
  }

  report->address_type = (address_.type() == common::DeviceAddress::Type::kLERandom)
                             ? hci::LEAddressType::kRandom
                             : hci::LEAddressType::kPublic;
  report->address = address_.value();
  report->length_data = adv_data_.GetSize();
  std::memcpy(report->data, adv_data_.GetData(), adv_data_.GetSize());

  WriteRandomRSSI(reinterpret_cast<int8_t*>(report->data + report->length_data));

  if (include_scan_rsp) {
    WriteScanResponseReport(reinterpret_cast<hci::LEAdvertisingReportData*>(
        report->data + report->length_data + sizeof(int8_t)));
  }

  return buffer;
}

common::DynamicByteBuffer FakeDevice::CreateScanResponseReportEvent() const {
  FTL_DCHECK(scannable_);
  size_t event_size = hci::EventPacket::GetMinBufferSize(
      sizeof(hci::LEMetaEventParams) + sizeof(hci::LEAdvertisingReportSubeventParams) +
      sizeof(hci::LEAdvertisingReportData) + scan_rsp_.GetSize() + sizeof(int8_t));
  common::DynamicByteBuffer buffer(event_size);
  hci::MutableEventPacket event_packet(hci::kLEMetaEventCode, &buffer);
  auto payload = event_packet.GetMutablePayload<hci::LEMetaEventParams>();
  payload->subevent_code = hci::kLEAdvertisingReportSubeventCode;

  auto subevent_payload =
      reinterpret_cast<hci::LEAdvertisingReportSubeventParams*>(payload->subevent_parameters);
  subevent_payload->num_reports = 1;

  auto report = reinterpret_cast<hci::LEAdvertisingReportData*>(subevent_payload->reports);
  WriteScanResponseReport(report);

  return buffer;
}

void FakeDevice::WriteScanResponseReport(hci::LEAdvertisingReportData* report) const {
  FTL_DCHECK(scannable_);
  report->event_type = hci::LEAdvertisingEventType::kScanRsp;
  report->address_type = (address_.type() == common::DeviceAddress::Type::kLERandom)
                             ? hci::LEAddressType::kRandom
                             : hci::LEAddressType::kPublic;
  report->address = address_.value();
  report->length_data = scan_rsp_.GetSize();
  std::memcpy(report->data, scan_rsp_.GetData(), scan_rsp_.GetSize());

  WriteRandomRSSI(reinterpret_cast<int8_t*>(report->data + report->length_data));
}

}  // namespace testing
}  // namespace bluetooth
