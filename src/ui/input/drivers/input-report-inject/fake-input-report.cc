// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-input-report.h"

#include <fbl/auto_lock.h>

namespace input_report_inject {

zx_status_t FakeInputReport::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  auto inst = std::make_unique<InputReportInstance>(zxdev());
  zx_status_t status = inst->Bind(this);
  if (status != ZX_OK) {
    return status;
  }

  {
    fbl::AutoLock lock(&instance_lock_);
    instance_list_.push_front(inst.get());
  }

  *dev_out = inst->zxdev();

  // devmgr is now in charge of the memory for inst.
  __UNUSED auto ptr = inst.release();
  return ZX_OK;
}

void FakeInputReport::ReceiveInput(fidl::VectorView<fuchsia_input_report::InputReport> reports) {
  fbl::AutoLock lock(&instance_lock_);
  for (InputReportInstance& instance : instance_list_) {
    for (const fuchsia_input_report::InputReport& report : reports) {
      instance.ReceiveReport(hid_input_report::ToInputReport(report));
    }
  }
}

void FakeInputReport::RemoveInstanceFromList(InputReportInstance* instance) {
  fbl::AutoLock lock(&instance_lock_);
  for (auto& iter : instance_list_) {
    if (iter.zxdev() == instance->zxdev()) {
      instance_list_.erase(iter);
      break;
    }
  }
}

const hid_input_report::ReportDescriptor* FakeInputReport::GetDescriptors(size_t* size) {
  *size = descriptors_.size();
  return descriptors_.data();
}

zx_status_t FakeInputReport::SendOutputReport(fuchsia_input_report::OutputReport report) {
  return ZX_ERR_NOT_SUPPORTED;
}

void FakeInputReport::ConvertDescriptors(const fuchsia_input_report::DeviceDescriptor& descriptor) {
  descriptors_.resize(0);

  if (descriptor.has_mouse()) {
    hid_input_report::ReportDescriptor desc = {};
    desc.descriptor = hid_input_report::ToMouseDescriptor(descriptor.mouse());
    descriptors_.push_back(std::move(desc));
  }

  if (descriptor.has_keyboard()) {
    hid_input_report::ReportDescriptor desc = {};
    desc.descriptor = hid_input_report::ToKeyboardDescriptor(descriptor.keyboard());
    descriptors_.push_back(std::move(desc));
  }

  if (descriptor.has_touch()) {
    hid_input_report::ReportDescriptor desc = {};
    desc.descriptor = hid_input_report::ToTouchDescriptor(descriptor.touch());
    descriptors_.push_back(std::move(desc));
  }

  if (descriptor.has_sensor()) {
    hid_input_report::ReportDescriptor desc = {};
    desc.descriptor = hid_input_report::ToSensorDescriptor(descriptor.sensor());
    descriptors_.push_back(std::move(desc));
  }
}

FakeInputReport* FakeInputReport::Create(zx_device_t* parent,
                                         fuchsia_input_report::DeviceDescriptor descriptor) {
  auto dev = std::make_unique<FakeInputReport>(parent);

  dev->ConvertDescriptors(descriptor);

  zx_status_t status = dev->DdkAdd("FakeInputReport");
  if (status != ZX_OK) {
    return nullptr;
  }

  // devmgr is now in charge of the memory for inst.
  return dev.release();
}

void FakeInputReport::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

}  // namespace input_report_inject
