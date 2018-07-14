// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fostr/zx_types.h"

#include <sstream>

#include <zx/channel.h>
#include <zx/event.h>
#include <zx/eventpair.h>
#include <zx/fifo.h>
#include <zx/guest.h>
#include <zx/interrupt.h>
#include <zx/job.h>
#include <zx/log.h>
#include <zx/port.h>
#include <zx/process.h>
#include <zx/resource.h>
#include <zx/socket.h>
#include <zx/thread.h>
#include <zx/time.h>
#include <zx/timer.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "gtest/gtest.h"
#include "lib/fsl/handles/object_info.h"

namespace fostr {
namespace {

// Matches string |value| from an istream.
std::istream& operator>>(std::istream& is, const std::string& value) {
  std::string str(value.size(), '\0');

  if (!is.read(&str[0], value.size()) || value != str) {
    return is;
  }

  // Required to set eofbit as appropriate.
  is.peek();

  return is;
}

// Tests invalid zx::channel formatting.
TEST(ZxTypes, InvalidChannel) {
  std::ostringstream os;
  zx::channel endpoint;

  os << endpoint;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::channel formatting.
TEST(ZxTypes, Channel) {
  std::ostringstream os;
  zx::channel endpoint0;
  zx::channel endpoint1;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

  os << endpoint0;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(endpoint0.get()), koid);
  EXPECT_EQ(fsl::GetKoid(endpoint1.get()), related_koid);
}

// Tests invalid zx::event formatting.
TEST(ZxTypes, InvalidEvent) {
  std::ostringstream os;
  zx::event event;

  os << event;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::event formatting.
TEST(ZxTypes, Event) {
  std::ostringstream os;
  zx::event event;
  EXPECT_EQ(ZX_OK, zx::event::create(0, &event));

  os << event;

  std::istringstream is(os.str());
  zx_koid_t koid;
  is >> "koid 0x" >> std::hex >> koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(event.get()), koid);
}

// Tests invalid zx::eventpair formatting.
TEST(ZxTypes, InvalidEventpair) {
  std::ostringstream os;
  zx::eventpair event;

  os << event;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::eventpair formatting.
TEST(ZxTypes, Eventpair) {
  std::ostringstream os;
  zx::eventpair event0;
  zx::eventpair event1;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &event0, &event1));

  os << event0;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(event0.get()), koid);
  EXPECT_EQ(fsl::GetKoid(event1.get()), related_koid);
}

// Tests invalid zx::fifo formatting.
TEST(ZxTypes, InvalidFifo) {
  std::ostringstream os;
  zx::fifo endpoint;

  os << endpoint;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::fifo formatting.
TEST(ZxTypes, Fifo) {
  std::ostringstream os;
  zx::fifo endpoint0;
  zx::fifo endpoint1;
  EXPECT_EQ(ZX_OK, zx::fifo::create(1, 1, 0, &endpoint0, &endpoint1));

  os << endpoint0;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(endpoint0.get()), koid);
  EXPECT_EQ(fsl::GetKoid(endpoint1.get()), related_koid);
}

// Tests invalid zx::guest formatting.
TEST(ZxTypes, InvalidGuest) {
  std::ostringstream os;
  zx::guest guest;

  os << guest;
  EXPECT_EQ("<invalid>", os.str());
}

// TODO(dalesat): Test valid zx::guest formatting.
// Don't know how to make a zx::guest.

// Tests invalid zx::interrupt formatting.
TEST(ZxTypes, InvalidInterrupt) {
  std::ostringstream os;
  zx::interrupt interrupt;

  os << interrupt;
  EXPECT_EQ("<invalid>", os.str());
}

// TODO(dalesat): Test valid zx::interrupt formatting.
// Don't know how to make a zx::interrupt.

// Tests invalid zx::job formatting.
TEST(ZxTypes, InvalidJob) {
  std::ostringstream os;
  zx::job job;

  os << job;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::job formatting.
TEST(ZxTypes, Job) {
  std::ostringstream os;
  zx::unowned<zx::job> job = zx::job::default_job();

  os << *job;

  std::istringstream is(os.str());
  zx_koid_t koid;
  is >> "koid 0x" >> std::hex >> koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(job->get()), koid);
}

// Tests invalid zx::log formatting.
TEST(ZxTypes, InvalidLog) {
  std::ostringstream os;
  zx::log log;

  os << log;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::log formatting.
TEST(ZxTypes, Log) {
  std::ostringstream os;
  zx::log log;
  EXPECT_EQ(ZX_OK, zx::log::create(0, &log));

  os << log;

  std::istringstream is(os.str());
  zx_koid_t koid;
  is >> "koid 0x" >> std::hex >> koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(log.get()), koid);
}

// Tests invalid zx::port formatting.
TEST(ZxTypes, InvalidPort) {
  std::ostringstream os;
  zx::port port;

  os << port;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::port formatting.
TEST(ZxTypes, Port) {
  std::ostringstream os;
  zx::port port;
  EXPECT_EQ(ZX_OK, zx::port::create(0, &port));

  os << port;

  std::istringstream is(os.str());
  zx_koid_t koid;
  is >> "koid 0x" >> std::hex >> koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(port.get()), koid);
}

// Tests invalid zx::process formatting.
TEST(ZxTypes, InvalidProcess) {
  std::ostringstream os;
  zx::process process;

  os << process;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::process formatting.
TEST(ZxTypes, Process) {
  std::ostringstream os;

  os << *zx::process::self();

  std::istringstream is(os.str());
  is >> fsl::GetObjectName(zx::process::self()->get());

  EXPECT_TRUE(is && is.eof());
}

// Tests invalid zx::resource formatting.
TEST(ZxTypes, InvalidResource) {
  std::ostringstream os;
  zx::resource resource;

  os << resource;
  EXPECT_EQ("<invalid>", os.str());
}

// TODO(dalesat): Test valid zx::resource formatting.
// Don't know how to make a zx::resource.

// Tests invalid zx::socket formatting.
TEST(ZxTypes, InvalidSocket) {
  std::ostringstream os;
  zx::socket endpoint;

  os << endpoint;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::socket formatting.
TEST(ZxTypes, Socket) {
  std::ostringstream os;
  zx::socket endpoint0;
  zx::socket endpoint1;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &endpoint0, &endpoint1));

  os << endpoint0;

  std::istringstream is(os.str());
  zx_koid_t koid;
  zx_koid_t related_koid;
  is >> "koid 0x" >> std::hex >> koid >> " <-> 0x" >> related_koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(endpoint0.get()), koid);
  EXPECT_EQ(fsl::GetKoid(endpoint1.get()), related_koid);
}

// Tests invalid zx::thread formatting.
TEST(ZxTypes, InvalidThread) {
  std::ostringstream os;
  zx::thread thread;

  os << thread;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::process formatting.
TEST(ZxTypes, Thread) {
  std::ostringstream os;

  os << *zx::thread::self();

  std::istringstream is(os.str());
  is >> fsl::GetObjectName(zx::thread::self()->get());

  EXPECT_TRUE(is && is.eof());
}

// Tests zero zx::duration formatting.
TEST(ZxTypes, ZeroDuration) {
  std::ostringstream os;
  zx::duration duration;

  os << duration;
  EXPECT_EQ("0", os.str());
}

// Tests infinite zx::duration formatting.
TEST(ZxTypes, InfiniteDuration) {
  std::ostringstream os;

  os << zx::duration::infinite();
  EXPECT_EQ("<infinite>", os.str());
}

// Tests zx::duration formatting.
TEST(ZxTypes, Duration) {
  std::ostringstream os;
  zx::duration duration(static_cast<zx_duration_t>(1234567890));

  os << duration;

  EXPECT_EQ("1.234,567,890", os.str());
}

// Tests invalid zx::timer formatting.
TEST(ZxTypes, InvalidTimer) {
  std::ostringstream os;
  zx::timer timer;

  os << timer;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::timer formatting.TODO

// Tests invalid zx::vmar formatting.
TEST(ZxTypes, InvalidVmar) {
  std::ostringstream os;
  zx::vmar vmar;

  os << vmar;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::vmar formatting.
TEST(ZxTypes, Vmar) {
  std::ostringstream os;

  os << *zx::vmar::root_self();

  std::istringstream is(os.str());
  zx_koid_t koid;
  is >> "koid 0x" >> std::hex >> koid;

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(zx::vmar::root_self()->get()), koid);
}

// Tests invalid zx::vmo formatting.
TEST(ZxTypes, InvalidVmo) {
  std::ostringstream os;
  zx::vmo vmo;

  os << vmo;
  EXPECT_EQ("<invalid>", os.str());
}

// Tests zx::vmo formatting.
TEST(ZxTypes, Vmo) {
  std::ostringstream os;
  zx::vmo vmo;
  uint64_t size = 1;
  EXPECT_EQ(ZX_OK, zx::vmo::create(size, 0, &vmo));

  os << vmo;

  std::istringstream is(os.str());
  zx_koid_t koid;
  is >> "koid 0x" >> std::hex >> koid >> ", " >> std::dec >> size >> " bytes";

  EXPECT_TRUE(is && is.eof());
  EXPECT_EQ(fsl::GetKoid(vmo.get()), koid);

  uint64_t actual_size;
  EXPECT_EQ(ZX_OK, vmo.get_size(&actual_size));
  EXPECT_EQ(actual_size, size);
}

}  // namespace
}  // namespace fostr
