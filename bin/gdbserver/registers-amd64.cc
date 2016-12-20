// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <cstring>

#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>

#include "lib/ftl/logging.h"

#include "thread.h"
#include "util.h"
#include "arch-x86.h"

namespace debugserver {
namespace arch {

int GetPCRegisterNumber() {
  return static_cast<int>(Amd64Register::RIP);
}

int GetFPRegisterNumber() {
  return static_cast<int>(Amd64Register::RBP);
}

int GetSPRegisterNumber() {
  return static_cast<int>(Amd64Register::RSP);
}

namespace {

// Includes all registers if |register_number| is -1.
// TODO: Here as elsewhere: more regsets.
std::string GetRegisterAsStringHelper(const mx_x86_64_general_regs_t& gregs,
                                      int regno) {
  FTL_DCHECK(regno >= -1 &&
             regno < static_cast<int>(Amd64Register::NUM_REGISTERS));

  // Based on the value of |regno|, we either need to fit in all
  // registers or just a single one.
  const size_t kDataSize = regno < 0 ? sizeof(gregs) : sizeof(uint64_t);
  const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&gregs);

  greg_bytes += regno < 0 ? 0 : regno * sizeof(uint64_t);

  return util::EncodeByteArrayString(greg_bytes, kDataSize);
}

class RegistersAmd64 final : public Registers {
 public:
  RegistersAmd64(Thread* thread) : Registers(thread) {
    memset(&gregs_, 0, sizeof(gregs_));
  }

  ~RegistersAmd64() = default;

  bool IsSupported() override { return true; }

  bool RefreshRegset(int regset) override {
    FTL_DCHECK(regset == 0);

    // We report all zeros for the registers if the thread was just created.
    if (thread()->state() == Thread::State::kNew) {
      memset(&gregs_, 0, sizeof(gregs_));
      return true;
    }

    uint32_t gregs_size;
    mx_status_t status = mx_thread_read_state(
        thread()->debug_handle(), regset, &gregs_, sizeof(gregs_), &gregs_size);
    if (status < 0) {
      util::LogErrorWithMxStatus("Failed to read x86_64 registers", status);
      return false;
    }

    FTL_DCHECK(gregs_size == sizeof(gregs_));

    FTL_VLOG(1) << "Regset " << regset << " refreshed";
    return true;
  }

  bool WriteRegset(int regset) override {
    FTL_DCHECK(regset == 0);

    mx_status_t status = mx_thread_write_state(thread()->debug_handle(), regset,
                                               &gregs_, sizeof(gregs_));
    if (status < 0) {
      util::LogErrorWithMxStatus("Failed to write x86_64 registers", status);
      return false;
    }

    FTL_VLOG(1) << "Regset " << regset << " written";
    return true;
  }

  std::string GetRegsetAsString(int regset) override {
    FTL_DCHECK(regset == 0);
    return GetRegisterAsStringHelper(gregs_, -1);
  }

  bool SetRegset(int regset, const ftl::StringView& value) override {
    FTL_DCHECK(regset == 0);

    auto bytes = util::DecodeByteArrayString(value);
    if (bytes.size() != sizeof(gregs_)) {
      FTL_LOG(ERROR) << "|value| doesn't match x86-64 general registers size: "
                     << value;
      return false;
    }

    memcpy(&gregs_, bytes.data(), sizeof(gregs_));
    FTL_VLOG(1) << "Regset " << regset << " cache written";
    return true;
  }

  std::string GetRegisterAsString(int regno) override {
    if (regno < 0 || regno >= static_cast<int>(Amd64Register::NUM_REGISTERS)) {
      FTL_LOG(ERROR) << "Bad register number: " << regno;
      return "";
    }

    return GetRegisterAsStringHelper(gregs_, regno);
  }

  bool GetRegister(int regno, void* buffer, size_t buf_size) override {
    if (regno < 0 || regno >= static_cast<int>(Amd64Register::NUM_REGISTERS)) {
      FTL_LOG(ERROR) << "Bad register_number: " << regno;
      return false;
    }
    if (buf_size != sizeof(uint64_t)) {
      FTL_LOG(ERROR) << "Bad buffer size: " << buf_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<const uint8_t*>(&gregs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(buffer, greg_bytes, buf_size);
    FTL_VLOG(1) << "Get register " << regno << " = "
                << util::EncodeByteArrayString(greg_bytes, buf_size);
    return true;
  }

  bool SetRegister(int regno, const void* value, size_t value_size) override {
    if (regno < 0 || regno >= static_cast<int>(Amd64Register::NUM_REGISTERS)) {
      FTL_LOG(ERROR) << "Invalid x86_64 register number: " << regno;
      return false;
    }
    // On x86_64 all general register values are 64-bit.
    if (value_size != sizeof(uint64_t)) {
      FTL_LOG(ERROR) << "Invalid x86_64 register value size: " << value_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<uint8_t*>(&gregs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(greg_bytes, value, value_size);
    FTL_VLOG(1) << "Set register " << regno << " = "
                << util::EncodeByteArrayString(greg_bytes, value_size);
    return true;
  }

  bool SetSingleStep(bool enable) override {
    if (enable)
      gregs_.rflags |= x86::EFLAGS_TF_MASK;
    else
      gregs_.rflags &= ~static_cast<uint64_t>(x86::EFLAGS_TF_MASK);
    FTL_VLOG(2) << "rflags.TF set to " << enable;
    return true;
  }

 private:
  mx_x86_64_general_regs_t gregs_;
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersAmd64(thread));
}

// static
std::string Registers::GetUninitializedGeneralRegistersAsString() {
  return std::string(sizeof(mx_x86_64_general_regs_t) * 2, '0');
}

// static
size_t Registers::GetRegisterSize() {
  return sizeof(uint64_t);
}

}  // namespace arch
}  // namespace debugserver
