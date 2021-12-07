// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/camera/bin/camera-gym/stream_cycler.h"

#include <fuchsia/camera/gym/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl_test_base.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl_test_base.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/camera/bin/camera-gym/call_stat.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace camera {

const uint64_t kFakeDeviceId = 23;  // Hard coded device ID
const uint32_t kFakeConfigId = 0;   // Hard coded config ID

class FakeAllocatorServ;
class FakeBufferCollectionTokenServ;
class FakeDeviceWatcherServ;
class FakeDeviceServ;
class FakeStreamServ;

// Some local abbreviations to make the code less verbose.
using Command = fuchsia::camera::gym::Command;

using SetConfigCommand = fuchsia::camera::gym::SetConfigCommand;
using AddStreamCommand = fuchsia::camera::gym::AddStreamCommand;
using SetCropCommand = fuchsia::camera::gym::SetCropCommand;
using SetResolutionCommand = fuchsia::camera::gym::SetResolutionCommand;

using BufferCollectionToken = fuchsia::sysmem::BufferCollectionToken;
using BufferCollectionTokenHandle = fuchsia::sysmem::BufferCollectionTokenHandle;

using AllocatorRequest = fidl::InterfaceRequest<fuchsia::sysmem::Allocator>;
using BufferCollectionTokenRequest = fidl::InterfaceRequest<BufferCollectionToken>;
using DeviceRequest = fidl::InterfaceRequest<fuchsia::camera3::Device>;
using DeviceWatcherRequest = fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher>;
using StreamRequest = fidl::InterfaceRequest<fuchsia::camera3::Stream>;

using CommandResult = fuchsia::camera::gym::Controller_SendCommand_Result;

using AllocateSharedCollectionHandler = fit::function<void(BufferCollectionTokenRequest)>;
using ConnectToDeviceHandler = fit::function<void(uint64_t, DeviceRequest)>;
using ConnectToStreamHandler = fit::function<void(uint32_t, StreamRequest)>;

///////////////////////////////////////////////////////////////////////////////////////////////////

// This is a "pile" of BufferCollectionTokenHandle's to be handed out.
// Handles are deposited using Put(), and withdrawn using Get().
class TokenHandleProvider {
 public:
  TokenHandleProvider() = default;
  ~TokenHandleProvider() = default;
  uint32_t Size() const { return handles_.size(); }
  BufferCollectionTokenHandle Get() {
    // If this assertion trips, it means the test did not set up enough handles.
    ZX_ASSERT(handles_.size() > 0);
    BufferCollectionTokenHandle handle = std::move(handles_[handles_.size() - 1]);
    handles_.pop_back();
    return handle;
  }
  void Put(BufferCollectionTokenHandle handle) { handles_.push_back(std::move(handle)); }

 private:
  std::vector<BufferCollectionTokenHandle> handles_;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

void NotImplemented(const std::string& name) { ZX_ASSERT(false); }

// FAKE FIDL SERVICES
//
// The following 5 classes implement 5 fake FIDL services to emulate:
//
// fuchsia.camera3.Device
// fuchsia.camera3.DeviceWatcher
// fuchsia.camera3.Stream
// fuchsia.sysmem.Allocator
// fuchsia.sysmem.BufferCollectionToken
//
// StreamCycler calls these FIDL services, so the fake services emulate the protocol exchange. The
// appropriate *_TestBase classes are used to support these implementation, so that they can be
// sparse.
//
// FakeBufferCollectionToken & FakeStream require support for multiple clients so they are
// slightly different. The difference is in the associated *ServerInst class.
//
// FakeStream also needs to return a BufferCollectionToken from WatchBufferCollection, so there
// is a one-off backdoor to inject that BufferCollectionToken.
//
// CallStat is used to track how many times each entry point of interest has been called and also
// the timestamp of the last call.
//
// All Xxx objects are owned and managed by a corresponding XxxServ object.
class FakeAllocator : public fuchsia::sysmem::testing::Allocator_TestBase {
 public:
  explicit FakeAllocator(FakeAllocatorServ* owner) : owner_(owner) {}
  void NotImplemented_(const std::string& name) override { NotImplemented(name); }

  // Owner
  FakeAllocatorServ* owner() { return owner_; }

  // FIDL API
  void AllocateSharedCollection(BufferCollectionTokenRequest token_request) override;

  // Stats/Counters
  CallStat* allocate_shared_collection_stat() { return &allocate_shared_collection_stat_; }

 private:
  FakeAllocatorServ* owner_;
  CallStat allocate_shared_collection_stat_;
};

// Supports multiple FakeBufferCollectionToken.
class FakeBufferCollectionToken : public fuchsia::sysmem::testing::BufferCollectionToken_TestBase {
 public:
  explicit FakeBufferCollectionToken(FakeBufferCollectionTokenServ* owner) : owner_(owner) {}
  void NotImplemented_(const std::string& name) override { NotImplemented(name); }

  // Owner
  FakeBufferCollectionTokenServ* owner() { return owner_; }

  // FIDL API
  void Duplicate(uint32_t mask, BufferCollectionTokenRequest request) override;
  void Sync(SyncCallback callback) override;

  // Stats/Counters
  CallStat* duplicate_stat() { return &duplicate_stat_; }
  CallStat* sync_stat() { return &sync_stat_; }

 private:
  FakeBufferCollectionTokenServ* owner_;
  CallStat duplicate_stat_;
  CallStat sync_stat_;
};

class FakeDevice : public fuchsia::camera3::testing::Device_TestBase {
 public:
  explicit FakeDevice(FakeDeviceServ* owner) : owner_(owner) {}
  void NotImplemented_(const std::string& name) override { NotImplemented(name); }

  // Owner
  FakeDeviceServ* owner() { return owner_; }

  // FIDL API
  void GetConfigurations(GetConfigurationsCallback callback) override;
  void WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) override;
  void SetCurrentConfiguration(uint32_t index) override;
  void WatchMuteState(WatchMuteStateCallback callback) override;
  void ConnectToStream(uint32_t id, StreamRequest request) override;

  // Stats/Counters
  CallStat* get_configurations_stat() { return &get_configurations_stat_; }
  CallStat* watch_current_configuration_stat() { return &watch_current_configuration_stat_; }
  CallStat* set_current_configuration_stat() { return &set_current_configuration_stat_; }
  CallStat* watch_mute_state_stat() { return &watch_mute_state_stat_; }
  CallStat* connect_to_stream_stat() { return &connect_to_stream_stat_; }

  void SetupConfigurations(std::vector<fuchsia::camera3::Configuration> configurations) {
    configurations_ = std::move(configurations);
  }

 private:
  FakeDeviceServ* owner_;
  CallStat get_configurations_stat_;
  CallStat watch_current_configuration_stat_;
  CallStat set_current_configuration_stat_;
  CallStat watch_mute_state_stat_;
  CallStat connect_to_stream_stat_;

  uint32_t watch_current_configuration_config_id_ = kFakeConfigId;

  std::vector<fuchsia::camera3::Configuration> configurations_;
};

class FakeDeviceWatcher : public fuchsia::camera3::testing::DeviceWatcher_TestBase {
 public:
  explicit FakeDeviceWatcher(FakeDeviceWatcherServ* owner) : owner_(owner) {}
  void NotImplemented_(const std::string& name) override { NotImplemented(name); }

  // Owner
  FakeDeviceWatcherServ* owner() { return owner_; }

  // FIDL API
  void WatchDevices(WatchDevicesCallback callback) override;
  void ConnectToDevice(uint64_t id, DeviceRequest request) override;

  // Stats/Counters
  CallStat* watch_devices_stat() { return &watch_devices_stat_; }
  CallStat* connect_to_device_stat() { return &connect_to_device_stat_; }

 private:
  FakeDeviceWatcherServ* owner_;
  CallStat watch_devices_stat_;
  CallStat connect_to_device_stat_;
};

// Supports multiple FakeStream.
class FakeStream : public fuchsia::camera3::testing::Stream_TestBase {
 public:
  explicit FakeStream(FakeStreamServ* owner) : owner_(owner) {}
  void NotImplemented_(const std::string& name) override { NotImplemented(name); }

  // Owner
  FakeStreamServ* owner() { return owner_; }

  // FIDL API
  void GetProperties(GetPropertiesCallback callback) override;
  void SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) override;
  void WatchCropRegion(WatchCropRegionCallback callback) override;
  void SetBufferCollection(BufferCollectionTokenHandle token) override;
  void WatchBufferCollection(WatchBufferCollectionCallback callback) override;
  void GetNextFrame(GetNextFrameCallback callback) override;

  // Stats/Counters
  CallStat* get_properties_stat() { return &get_properties_stat_; }
  CallStat* set_crop_region_stat() { return &set_crop_region_stat_; }
  CallStat* watch_crop_region_stat() { return &watch_crop_region_stat_; }
  CallStat* set_buffer_collection_stat() { return &set_buffer_collection_stat_; }
  CallStat* watch_buffer_collection_stat() { return &watch_buffer_collection_stat_; }
  CallStat* get_next_frame_stat() { return &get_next_frame_stat_; }

  // Handlers
  void set_token_handle_provider(TokenHandleProvider* provider) {
    token_handle_provider_ = provider;
  }
  TokenHandleProvider* token_handle_provider() { return token_handle_provider_; }

 private:
  FakeStreamServ* owner_;
  CallStat get_properties_stat_;
  CallStat set_crop_region_stat_;
  CallStat watch_crop_region_stat_;
  CallStat set_buffer_collection_stat_;
  CallStat watch_buffer_collection_stat_;
  CallStat get_next_frame_stat_;

  TokenHandleProvider* token_handle_provider_;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

// FAKE SERVICE INSTANCES
//
// Roughly following the example spelled out in:
//
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/hlcpp/topics/testing
//
// The service instance takes care of binding the fake implementation to the interface request.
class FakeAllocatorServ {
 public:
  explicit FakeAllocatorServ(sys::ComponentContext* context);
  ~FakeAllocatorServ();
  void OnNewRequest(AllocatorRequest request);
  FakeAllocator* impl() { return impl_; }

  // Handlers for FakeAllocator.
  void SetHandlers(AllocateSharedCollectionHandler allocate_shared_collection_handler) {
    allocate_shared_collection_handler_ = std::move(allocate_shared_collection_handler);
  }
  void OnAllocateSharedCollection(BufferCollectionTokenRequest request) {
    ZX_ASSERT(allocate_shared_collection_handler_);
    allocate_shared_collection_handler_(std::move(request));
  }

 private:
  FakeAllocator* impl_;
  fidl::Binding<fuchsia::sysmem::Allocator>* binding_;
  sys::ComponentContext* context_;

  AllocateSharedCollectionHandler allocate_shared_collection_handler_;
};

class FakeBufferCollectionTokenServ {
 public:
  explicit FakeBufferCollectionTokenServ(sys::ComponentContext* context);
  void OnNewRequest(BufferCollectionTokenRequest request);
  FakeBufferCollectionToken* impl(uint32_t client_index) {
    ZX_ASSERT(client_index < clients_.size());
    return clients_[client_index].impl.get();
  }
  uint32_t clients_size() { return clients_.size(); }

  // Gates
  void set_sync_remaining(uint32_t remaining) { sync_remaining_ = remaining; }
  uint32_t sync_remaining() { return sync_remaining_; }
  void dec_sync_remaining() { --sync_remaining_; }

 private:
  struct Client {
    std::unique_ptr<FakeBufferCollectionToken> impl;
    std::unique_ptr<fidl::Binding<BufferCollectionToken>> binding;
  };
  uint32_t client_count_ = 0;
  std::vector<Client> clients_;
  sys::ComponentContext* context_;

  uint32_t sync_remaining_ = 0;
};

class FakeDeviceServ {
 public:
  explicit FakeDeviceServ(sys::ComponentContext* context);
  void OnNewRequest(DeviceRequest request);
  FakeDevice* impl() { return impl_.get(); }

  // Handlers for FakeDevice.
  void SetHandlers(ConnectToStreamHandler connect_to_stream_handler) {
    connect_to_stream_handler_ = std::move(connect_to_stream_handler);
  }
  void OnConnectToStream(uint32_t id, StreamRequest request) {
    ZX_ASSERT(connect_to_stream_handler_);
    connect_to_stream_handler_(id, std::move(request));
  }

  // Gates
  void set_watch_current_configuration_remaining(uint32_t remaining) {
    watch_current_configuration_remaining_ = remaining;
  }
  uint32_t watch_current_configuration_remaining() {
    return watch_current_configuration_remaining_;
  }
  void dec_watch_current_configuration_remaining() { --watch_current_configuration_remaining_; }

 private:
  std::unique_ptr<FakeDevice> impl_;
  std::unique_ptr<fidl::Binding<fuchsia::camera3::Device>> binding_;
  sys::ComponentContext* context_;
  ConnectToStreamHandler connect_to_stream_handler_;

  uint32_t watch_current_configuration_remaining_ = 0;
};

class FakeDeviceWatcherServ {
 public:
  explicit FakeDeviceWatcherServ(sys::ComponentContext* context);
  void OnNewRequest(DeviceWatcherRequest request);
  FakeDeviceWatcher* impl() { return impl_.get(); }

  // Handlers for FakeDeviceWatcher
  void SetHandlers(ConnectToDeviceHandler connect_to_device_handler) {
    connect_to_device_handler_ = std::move(connect_to_device_handler);
  }
  void OnConnectToDevice(uint64_t id, DeviceRequest request) {
    ZX_ASSERT(connect_to_device_handler_);
    connect_to_device_handler_(id, std::move(request));
  }

  // Gates
  void set_watch_devices_remaining(uint32_t remaining) { watch_devices_remaining_ = remaining; }
  uint32_t watch_devices_remaining() { return watch_devices_remaining_; }
  void dec_watch_devices_remaining() { --watch_devices_remaining_; }

 private:
  std::unique_ptr<FakeDeviceWatcher> impl_;
  std::unique_ptr<fidl::Binding<fuchsia::camera3::DeviceWatcher>> binding_;
  sys::ComponentContext* context_;
  ConnectToDeviceHandler connect_to_device_handler_;

  uint32_t watch_devices_remaining_ = 0;
};

class FakeStreamServ {
 public:
  explicit FakeStreamServ(sys::ComponentContext* context);
  void OnNewRequest(StreamRequest request);
  FakeStream* impl(uint32_t client_index) {
    ZX_ASSERT(client_index < clients_.size());
    return clients_[client_index].impl.get();
  }
  uint32_t clients_size() { return clients_.size(); }

  TokenHandleProvider* token_handle_provider() { return &token_handle_provider_; }

  // Gates
  void set_watch_buffer_collection_remaining(uint32_t remaining) {
    watch_buffer_collection_remaining_ = remaining;
  }
  uint32_t watch_buffer_collection_remaining() { return watch_buffer_collection_remaining_; }
  void dec_watch_buffer_collection_remaining() { --watch_buffer_collection_remaining_; }

 private:
  struct Client {
    std::unique_ptr<FakeStream> impl;
    std::unique_ptr<fidl::Binding<fuchsia::camera3::Stream>> binding;
  };
  uint32_t client_count_ = 0;
  std::vector<Client> clients_;
  sys::ComponentContext* context_;
  TokenHandleProvider token_handle_provider_;

  uint32_t watch_buffer_collection_remaining_ = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

void FakeAllocator::AllocateSharedCollection(BufferCollectionTokenRequest token_request) {
  allocate_shared_collection_stat()->Enter();
  owner()->OnAllocateSharedCollection(std::move(token_request));
}

void FakeBufferCollectionToken::Duplicate(uint32_t mask, BufferCollectionTokenRequest request) {
  duplicate_stat()->Enter();
}

void FakeBufferCollectionToken::Sync(SyncCallback callback) {
  sync_stat()->Enter();
  if (owner()->sync_remaining() > 0) {
    owner()->dec_sync_remaining();
    callback();
  }
}

void FakeDevice::GetConfigurations(GetConfigurationsCallback callback) {
  get_configurations_stat()->Enter();
  callback(fidl::Clone(configurations_));
}

void FakeDevice::WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) {
  watch_current_configuration_stat()->Enter();
  if (owner()->watch_current_configuration_remaining() > 0) {
    owner()->dec_watch_current_configuration_remaining();
    callback(watch_current_configuration_config_id_);
  }
}

void FakeDevice::SetCurrentConfiguration(uint32_t index) {
  set_current_configuration_stat()->Enter();
}

void FakeDevice::WatchMuteState(WatchMuteStateCallback callback) {
  watch_mute_state_stat()->Enter();
}

void FakeDevice::ConnectToStream(uint32_t id, StreamRequest request) {
  connect_to_stream_stat()->Enter();
  owner()->OnConnectToStream(id, std::move(request));
}

void FakeDeviceWatcher::WatchDevices(WatchDevicesCallback callback) {
  watch_devices_stat()->Enter();
  if (owner()->watch_devices_remaining() > 0) {
    owner()->dec_watch_devices_remaining();
    std::vector<fuchsia::camera3::WatchDevicesEvent> events;
    events.push_back({});
    events[0].set_added(kFakeDeviceId);
    callback(std::move(events));
  }
}

void FakeDeviceWatcher::ConnectToDevice(uint64_t id, DeviceRequest request) {
  connect_to_device_stat()->Enter();
  owner()->OnConnectToDevice(id, std::move(request));
}

void FakeStream::GetProperties(GetPropertiesCallback callback) { get_properties_stat()->Enter(); }

void FakeStream::SetCropRegion(std::unique_ptr<fuchsia::math::RectF> region) {
  set_crop_region_stat()->Enter();
}

void FakeStream::WatchCropRegion(WatchCropRegionCallback callback) {
  watch_crop_region_stat()->Enter();
}

void FakeStream::SetBufferCollection(BufferCollectionTokenHandle token) {
  set_buffer_collection_stat()->Enter();
}

void FakeStream::WatchBufferCollection(WatchBufferCollectionCallback callback) {
  watch_buffer_collection_stat()->Enter();
  if (owner()->watch_buffer_collection_remaining() > 0) {
    owner()->dec_watch_buffer_collection_remaining();
    EXPECT_GT(token_handle_provider()->Size(), 0U);
    auto token_handle = token_handle_provider()->Get();
    callback(std::move(token_handle));
  }
}

void FakeStream::GetNextFrame(GetNextFrameCallback callback) { get_next_frame_stat()->Enter(); }

///////////////////////////////////////////////////////////////////////////////////////////////////

FakeAllocatorServ::FakeAllocatorServ(sys::ComponentContext* context) {
  context_ = context;
  impl_ = new FakeAllocator(this);
  binding_ = new fidl::Binding<fuchsia::sysmem::Allocator>(impl_);
  fidl::InterfaceRequestHandler<fuchsia::sysmem::Allocator> handler =
      [&](AllocatorRequest request) { binding_->Bind(std::move(request)); };
  context_->outgoing()->AddPublicService(std::move(handler));
}

FakeAllocatorServ::~FakeAllocatorServ() {
  if (binding_) {
    delete binding_;
    binding_ = nullptr;
  }
  if (impl_) {
    delete impl_;
    impl_ = nullptr;
  }
}

FakeBufferCollectionTokenServ::FakeBufferCollectionTokenServ(sys::ComponentContext* context) {
  context_ = context;
}

void FakeBufferCollectionTokenServ::OnNewRequest(BufferCollectionTokenRequest request) {
  Client client;
  client.impl = std::make_unique<FakeBufferCollectionToken>(this);
  client.binding = std::make_unique<fidl::Binding<BufferCollectionToken>>(client.impl.get());
  client.binding->Bind(std::move(request));
  client_count_++;
  clients_.push_back(std::move(client));
}

FakeDeviceServ::FakeDeviceServ(sys::ComponentContext* context) {
  context_ = context;
  impl_ = std::make_unique<FakeDevice>(this);
  binding_ = std::make_unique<fidl::Binding<fuchsia::camera3::Device>>(impl_.get());
}

void FakeDeviceServ::OnNewRequest(DeviceRequest request) { binding_->Bind(std::move(request)); }

FakeDeviceWatcherServ::FakeDeviceWatcherServ(sys::ComponentContext* context) {
  context_ = context;
  impl_ = std::make_unique<FakeDeviceWatcher>(this);
  binding_ = std::make_unique<fidl::Binding<fuchsia::camera3::DeviceWatcher>>(impl_.get());
  fidl::InterfaceRequestHandler<fuchsia::camera3::DeviceWatcher> handler =
      [&](DeviceWatcherRequest request) { binding_->Bind(std::move(request)); };
  context_->outgoing()->AddPublicService(std::move(handler));
}

FakeStreamServ::FakeStreamServ(sys::ComponentContext* context) { context_ = context; }

void FakeStreamServ::OnNewRequest(StreamRequest request) {
  Client client;
  client.impl = std::make_unique<FakeStream>(this);
  client.impl->set_token_handle_provider(&token_handle_provider_);
  client.binding = std::make_unique<fidl::Binding<fuchsia::camera3::Stream>>(client.impl.get());
  client.binding->Bind(std::move(request));
  client_count_++;
  clients_.push_back(std::move(client));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class ExtraLoop : public gtest::TestLoopFixture {
 public:
  void SetUp() override { TestLoopFixture::SetUp(); }
  void TearDown() override { TestLoopFixture::TearDown(); }
  void RunUntilIdle() { RunLoopUntilIdle(); }
  async_dispatcher_t* get_dispatcher() { return dispatcher(); }

 private:
  void TestBody() override {}
};

class StreamCyclerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    allocator_serv_.reset(new FakeAllocatorServ(provider_.context()));
    buffer_collection_token_serv_.reset(new FakeBufferCollectionTokenServ(provider_.context()));
    device_watcher_serv_.reset(new FakeDeviceWatcherServ(provider_.context()));
    device_serv_.reset(new FakeDeviceServ(provider_.context()));
    stream_serv_.reset(new FakeStreamServ(provider_.context()));

    controller_loop_.reset(new ExtraLoop);

    SetupImplHandlers();
  }
  void TearDown() override {
    TestLoopFixture::TearDown();
    allocator_serv_.reset();
    buffer_collection_token_serv_.reset();
    device_watcher_serv_.reset();
    device_serv_.reset();
    stream_serv_.reset();
  }

  void SetupImplHandlers() {
    AllocateSharedCollectionHandler allocate_shared_collection_handler =
        [this](BufferCollectionTokenRequest token_request) {
          buffer_collection_token_serv()->OnNewRequest(std::move(token_request));
        };
    ConnectToStreamHandler connect_to_stream_handler = [this](uint32_t id, StreamRequest request) {
      stream_serv()->OnNewRequest(std::move(request));
    };
    ConnectToDeviceHandler connect_to_device_handler = [this](uint64_t id, DeviceRequest request) {
      device_serv()->OnNewRequest(std::move(request));
    };

    // Set up handlers!
    allocator_serv()->SetHandlers(std::move(allocate_shared_collection_handler));
    device_serv()->SetHandlers(std::move(connect_to_stream_handler));
    device_watcher_serv()->SetHandlers(std::move(connect_to_device_handler));
  }

  // Set up fake BufferCollectionTokenHandles to be used in callbacks from WatchBufferCollection.
  void SetupFakeTokenHandles(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
      BufferCollectionTokenHandle token_handle;
      auto _unused_server_end = token_handle.NewRequest();
      stream_serv()->token_handle_provider()->Put(std::move(token_handle));
    }
  }

  // Fake StreamCycler handler stubs.
  uint32_t OnAddCollection(BufferCollectionTokenHandle token,
                           fuchsia::sysmem::ImageFormat_2 image_format, std::string description) {
    on_add_collection_stat()->Enter();
    set_on_add_collection_width(image_format.coded_width);
    set_on_add_collection_height(image_format.coded_height);
    return 0;
  }
  void OnRemoveCollection(uint32_t id) {
    on_remove_collection_stat()->Enter();
    set_on_remove_collection_id(id);
  }
  void OnShowBuffer(uint32_t collection_id, uint32_t buffer_index, zx::eventpair release_fence,
                    std::optional<fuchsia::math::RectF> subregion) {
    on_show_buffer_stat()->Enter();
    set_on_show_buffer_id(collection_id);
    set_on_show_buffer_index(buffer_index);
  }
  void OnMuteChanged(bool muted) {
    on_mute_changed_stat()->Enter();
    set_on_mute_changed_muted(muted);
  }
  void SetupHandlers(StreamCycler* cycler) {
    cycler->SetHandlers(fit::bind_member(this, &StreamCyclerTest::OnAddCollection),
                        fit::bind_member(this, &StreamCyclerTest::OnRemoveCollection),
                        fit::bind_member(this, &StreamCyclerTest::OnShowBuffer),
                        fit::bind_member(this, &StreamCyclerTest::OnMuteChanged));
  }

  // Create a StreamCycler with a fake DeviceWater and a fake Allocator.
  std::unique_ptr<StreamCycler> CreateStreamCycler(bool manual_mode) {
    fuchsia::camera3::DeviceWatcherHandle device_watcher;
    context_provider().ConnectToPublicService(device_watcher.NewRequest());
    fuchsia::sysmem::AllocatorHandle allocator;
    context_provider().ConnectToPublicService(allocator.NewRequest());
    auto cycler_result = StreamCycler::Create(std::move(device_watcher), std::move(allocator),
                                              dispatcher(), manual_mode);
    EXPECT_FALSE(cycler_result.is_error());
    ZX_ASSERT(!(cycler_result.is_error()));  // Do not continue if StreamCycler can't be created.
    auto cycler = cycler_result.take_value();
    cycler->set_controller_dispatcher(controller_loop_->get_dispatcher());
    return cycler;
  }

  // "Simple" configuration is where there is only 1 configuration supported by the fake device,
  // and there is only 1 stream in that configuration.
  void SetupFakeSimpleConfigurations() {
    std::vector<fuchsia::camera3::Configuration> configurations;
    configurations.clear();

    // Config 0
    configurations.push_back({});

    // Config 0 stream 0
    configurations[0].streams.push_back({});
    configurations[0].streams[0].image_format.coded_width = 2345;
    configurations[0].streams[0].image_format.coded_height = 789;
    configurations[0].streams[0].image_format.bytes_per_row = 2345;

    device_impl()->SetupConfigurations(std::move(configurations));
  }

  // "Complex" configuration is where there is only 3 configurations supported by the fake device,
  // and there are 3, 2 & 2 streams in those configurations.
  void SetupFakeComplexConfigurations() {
    std::vector<fuchsia::camera3::Configuration> configurations;
    configurations.clear();

    // Config 0
    configurations.push_back({});

    // Config 0 stream 0
    configurations[0].streams.push_back({});
    configurations[0].streams[0].image_format.coded_width = 1234;
    configurations[0].streams[0].image_format.coded_height = 678;
    configurations[0].streams[0].image_format.bytes_per_row = 1234;

    // Config 0 stream 1
    configurations[0].streams.push_back({});
    configurations[0].streams[1].image_format.coded_width = 864;
    configurations[0].streams[1].image_format.coded_height = 468;
    configurations[0].streams[1].image_format.bytes_per_row = 864;

    // Config 0 stream 2
    configurations[0].streams.push_back({});
    configurations[0].streams[2].image_format.coded_width = 654;
    configurations[0].streams[2].image_format.coded_height = 432;
    configurations[0].streams[2].image_format.bytes_per_row = 654;

    // Config 1
    configurations.push_back({});

    // Config 1 stream 0
    configurations[1].streams.push_back({});
    configurations[1].streams[0].image_format.coded_width = 876;
    configurations[1].streams[0].image_format.coded_height = 456;
    configurations[1].streams[0].image_format.bytes_per_row = 876;

    // Config 1 stream 1
    configurations[1].streams.push_back({});
    configurations[1].streams[1].image_format.coded_width = 576;
    configurations[1].streams[1].image_format.coded_height = 392;
    configurations[1].streams[1].image_format.bytes_per_row = 576;

    // Config 2
    configurations.push_back({});

    // Config 2 stream 0
    configurations[2].streams.push_back({});
    configurations[2].streams[0].image_format.coded_width = 744;
    configurations[2].streams[0].image_format.coded_height = 588;
    configurations[2].streams[0].image_format.bytes_per_row = 744;

    // Config 2 stream 1
    configurations[2].streams.push_back({});
    configurations[2].streams[1].image_format.coded_width = 468;
    configurations[2].streams[1].image_format.coded_height = 345;
    configurations[2].streams[1].image_format.bytes_per_row = 468;

    device_impl()->SetupConfigurations(std::move(configurations));
  }

  // SetupGatesToStopAt*() - These are gates that either allow StreamCycler execution to continue by
  // responding with a callback, or block it by not responding with a callback.
  //
  // This gating allows tests to break up the code into successive sections to be tested, with the
  // assumption that the prior sections were functioning correctly.
  void SetupGatesToStopAtWatchDevices() {
    // Turn off all callback gates so that execution will stop at the end of Create().
    device_watcher_serv()->set_watch_devices_remaining(0);
    device_serv()->set_watch_current_configuration_remaining(0);
    buffer_collection_token_serv()->set_sync_remaining(0);
    stream_serv()->set_watch_buffer_collection_remaining(0);
  }
  void SetupGatesToStopAtWatchCurrentConfiguration() {
    // Turn on 1 callback gate so that execution will stop at WatchDeviceCallback().
    device_watcher_serv()->set_watch_devices_remaining(1);
    device_serv()->set_watch_current_configuration_remaining(0);
    buffer_collection_token_serv()->set_sync_remaining(0);
    stream_serv()->set_watch_buffer_collection_remaining(0);
  }
  void SetupGatesToStopAtSync() {
    // Turn on callback gates so that execution will stop at Sync().
    device_watcher_serv()->set_watch_devices_remaining(1);
    device_serv()->set_watch_current_configuration_remaining(1);
    buffer_collection_token_serv()->set_sync_remaining(0);
    stream_serv()->set_watch_buffer_collection_remaining(0);
  }
  void SetupGatesToStopAtWatchBufferCollection(uint32_t stream_count) {
    // Turn on callback gates so that execution will stop at WatchBufferCollection().
    device_watcher_serv()->set_watch_devices_remaining(1);
    device_serv()->set_watch_current_configuration_remaining(1);
    buffer_collection_token_serv()->set_sync_remaining(stream_count);
    stream_serv()->set_watch_buffer_collection_remaining(0);
  }
  void SetupGatesToStopAtGetNextFrame(uint32_t stream_count) {
    // Turn on callback gates so that execution will stop at GetNextFrame().
    device_watcher_serv()->set_watch_devices_remaining(1);
    device_serv()->set_watch_current_configuration_remaining(1);
    buffer_collection_token_serv()->set_sync_remaining(stream_count);
    stream_serv()->set_watch_buffer_collection_remaining(stream_count);
  }

  // SetupCycler*() - Create a StreamCycler to be tested and set up the fakes + controls to allow
  // this StreamCycler to execute to a specific point.
  std::unique_ptr<StreamCycler> SetupCyclerStopAtWatchDevices(bool manual_mode) {
    SetupGatesToStopAtWatchDevices();
    auto cycler = CreateStreamCycler(manual_mode);
    SetupHandlers(cycler.get());
    return cycler;
  }
  std::unique_ptr<StreamCycler> SetupCyclerStopAtWatchCurrentConfiguration(bool manual_mode) {
    SetupGatesToStopAtWatchCurrentConfiguration();
    auto cycler = CreateStreamCycler(manual_mode);
    SetupHandlers(cycler.get());
    return cycler;
  }
  std::unique_ptr<StreamCycler> SetupCyclerStopAtSync(bool manual_mode) {
    SetupGatesToStopAtSync();
    auto cycler = CreateStreamCycler(manual_mode);
    SetupHandlers(cycler.get());
    return cycler;
  }
  std::unique_ptr<StreamCycler> SetupCyclerStopAtWatchBufferCollection(bool manual_mode,
                                                                       uint32_t stream_count) {
    SetupGatesToStopAtWatchBufferCollection(stream_count);
    auto cycler = CreateStreamCycler(manual_mode);
    SetupHandlers(cycler.get());
    SetupFakeTokenHandles(stream_count);
    return cycler;
  }
  std::unique_ptr<StreamCycler> SetupCyclerStopAtGetNextFrame(bool manual_mode,
                                                              uint32_t stream_count) {
    SetupGatesToStopAtGetNextFrame(stream_count);
    auto cycler = CreateStreamCycler(manual_mode);
    SetupHandlers(cycler.get());
    SetupFakeTokenHandles(stream_count);
    return cycler;
  }

  // Construct valid SetConfigCommand and call ExecuteSetConfigCommand().
  void SetupAndInvokeExecuteSetConfigCommand(StreamCycler* cycler, uint32_t config_id) {
    SetConfigCommand set_config_command;
    set_config_command.config_id = config_id;
    set_config_command.async = false;
    Command command = Command::WithSetConfig(std::move(set_config_command));
    cycler->ExecuteCommand(std::move(command),
                           [this](fuchsia::camera::gym::Controller_SendCommand_Result result) {
                             execute_set_config_command_stat()->Enter();
                             set_execute_set_config_command_result(std::move(result));
                           });
  }

  // Construct AddStreamCommand and call ExecuteAddStreamCommand().
  void SetupAndInvokeExecuteAddStreamCommand(StreamCycler* cycler, uint32_t stream_id) {
    AddStreamCommand add_stream_command;
    add_stream_command.stream_id = stream_id;
    add_stream_command.async = false;
    Command command = Command::WithAddStream(std::move(add_stream_command));
    cycler->ExecuteCommand(std::move(command),
                           [this](fuchsia::camera::gym::Controller_SendCommand_Result result) {
                             execute_add_stream_command_stat()->Enter();
                             set_execute_add_stream_command_result(std::move(result));
                           });
  }

  // Construct SetCropCommand and call ExecuteSetCropCommand().
  void SetupAndInvokeExecuteSetCropCommand(StreamCycler* cycler, uint32_t stream_id, float x,
                                           float y, float width, float height) {
    SetCropCommand set_crop_command;
    set_crop_command.stream_id = stream_id;
    set_crop_command.x = x;
    set_crop_command.y = y;
    set_crop_command.width = width;
    set_crop_command.height = height;
    set_crop_command.async = false;
    Command command = Command::WithSetCrop(std::move(set_crop_command));
    cycler->ExecuteCommand(std::move(command),
                           [this](fuchsia::camera::gym::Controller_SendCommand_Result result) {
                             execute_set_crop_command_stat()->Enter();
                           });
  }

  sys::ComponentContext* context() { return provider_.context(); }
  sys::testing::ComponentContextProvider& context_provider() { return provider_; }

  FakeAllocatorServ* allocator_serv() { return allocator_serv_.get(); }
  FakeBufferCollectionTokenServ* buffer_collection_token_serv() {
    return buffer_collection_token_serv_.get();
  }
  FakeDeviceWatcherServ* device_watcher_serv() { return device_watcher_serv_.get(); }
  FakeDeviceServ* device_serv() { return device_serv_.get(); }
  FakeStreamServ* stream_serv() { return stream_serv_.get(); }

  FakeAllocator* allocator_impl() { return allocator_serv()->impl(); }
  FakeBufferCollectionToken* buffer_collection_token_impl(uint32_t client_index) {
    return buffer_collection_token_serv()->impl(client_index);
  }
  FakeDeviceWatcher* device_watcher_impl() { return device_watcher_serv()->impl(); }
  FakeDevice* device_impl() { return device_serv()->impl(); }
  FakeStream* stream_impl(uint32_t client_index) { return stream_serv()->impl(client_index); }

  CallStat* execute_set_config_command_stat() { return &execute_set_config_command_stat_; }
  CallStat* execute_add_stream_command_stat() { return &execute_add_stream_command_stat_; }
  CallStat* execute_set_crop_command_stat() { return &execute_set_crop_command_stat_; }

  CallStat* on_add_collection_stat() { return &on_add_collection_stat_; }
  CallStat* on_remove_collection_stat() { return &on_remove_collection_stat_; }
  CallStat* on_show_buffer_stat() { return &on_show_buffer_stat_; }
  CallStat* on_mute_changed_stat() { return &on_mute_changed_stat_; }

  float on_add_collection_width() { return on_add_collection_width_; }
  float on_add_collection_height() { return on_add_collection_height_; }
  uint32_t on_remove_collection_id() { return on_remove_collection_id_; }
  uint32_t on_show_buffer_id() { return on_show_buffer_id_; }
  uint32_t on_show_buffer_index() { return on_show_buffer_index_; }
  bool on_mute_changed() { return on_mute_changed_muted_; }

  void set_on_add_collection_width(float width) { on_add_collection_width_ = width; }
  void set_on_add_collection_height(float height) { on_add_collection_height_ = height; }
  void set_on_remove_collection_id(uint32_t id) { on_remove_collection_id_ = id; }
  void set_on_show_buffer_id(uint32_t id) { on_show_buffer_id_ = id; }
  void set_on_show_buffer_index(uint32_t index) { on_show_buffer_index_ = index; }
  void set_on_mute_changed_muted(bool muted) { on_mute_changed_muted_ = muted; }

  CommandResult execute_set_config_command_result() {
    return fidl::Clone(execute_set_config_command_result_);
  }
  CommandResult execute_add_stream_command_result() {
    return fidl::Clone(execute_add_stream_command_result_);
  }

  void set_execute_set_config_command_result(CommandResult result) {
    execute_set_config_command_result_ = std::move(result);
  }
  void set_execute_add_stream_command_result(CommandResult result) {
    execute_add_stream_command_result_ = std::move(result);
  }

  void RunControllerLoopUntilIdle() { controller_loop_->RunUntilIdle(); }

 private:
  // Need to be alive for duration of use of context.
  sys::testing::ComponentContextProvider provider_;

  CallStat execute_set_config_command_stat_;
  CallStat execute_add_stream_command_stat_;
  CallStat execute_set_crop_command_stat_;

  CallStat on_add_collection_stat_;
  CallStat on_remove_collection_stat_;
  CallStat on_show_buffer_stat_;
  CallStat on_mute_changed_stat_;

  std::unique_ptr<FakeAllocatorServ> allocator_serv_;
  std::unique_ptr<FakeBufferCollectionTokenServ> buffer_collection_token_serv_;
  std::unique_ptr<FakeDeviceWatcherServ> device_watcher_serv_;
  std::unique_ptr<FakeDeviceServ> device_serv_;
  std::unique_ptr<FakeStreamServ> stream_serv_;

  std::unique_ptr<ExtraLoop> controller_loop_;

  float on_add_collection_width_ = 0.0;
  float on_add_collection_height_ = 0.0;
  uint32_t on_remove_collection_id_ = 0;
  uint32_t on_show_buffer_id_ = 0;
  uint32_t on_show_buffer_index_ = 0;
  bool on_mute_changed_muted_ = false;

  CommandResult execute_set_config_command_result_;
  CommandResult execute_add_stream_command_result_;
};

// Test Create()
TEST_F(StreamCyclerTest, SimpleConfiguration_AutomaticMode_Create) {
  // Use Create() in automatic mode to construct a StreamCycler.
  auto cycler = SetupCyclerStopAtWatchDevices(false /* manual_mode */);

  // TEST: WatchDevices() should be called.
  RunLoopUntilIdle();

  // VERIFY:
  EXPECT_EQ(device_watcher_impl()->watch_devices_stat()->call_counter(), 1U);
}

// Test WatchDevicesCallback()
TEST_F(StreamCyclerTest, SimpleConfiguration_AutomaticMode_WatchDevicesCallback) {
  // Use Create() in automatic mode to construct a StreamCycler.
  auto cycler = SetupCyclerStopAtWatchCurrentConfiguration(false /* manual_mode */);
  SetupFakeSimpleConfigurations();

  // TEST: WatchCurrentConfiguration() should be called.
  RunLoopUntilIdle();

  // VERIFY:
  EXPECT_EQ(device_watcher_impl()->watch_devices_stat()->call_counter(), 2U);
  EXPECT_EQ(device_watcher_impl()->connect_to_device_stat()->call_counter(), 1U);
  EXPECT_EQ(device_impl()->get_configurations_stat()->call_counter(), 1U);
  EXPECT_EQ(device_impl()->watch_current_configuration_stat()->call_counter(), 1U);
  EXPECT_EQ(device_impl()->watch_mute_state_stat()->call_counter(), 1U);
}

// Test WatchCurrentConfigurationCallback()
//
// WatchCurrentConfigurationCallback() is more complex in automatic mode as it does daisy-chain to
// other functions. Create() indirectly invokes WatchDevicesCallback(), which should indirectly
// invoke WatchCurrentConfigurationCallback(). The daisy-chaining is stopped at the Sync() call.
TEST_F(StreamCyclerTest, SimpleConfiguration_AutomaticMode_WatchCurrentConfigurationCallback) {
  // Use Create() in automatic mode to construct a StreamCycler.
  auto cycler = SetupCyclerStopAtSync(false /* manual_mode */);
  SetupFakeSimpleConfigurations();

  // TEST: ConnectToStream() should be called.
  RunLoopUntilIdle();

  // VERIFY:
  EXPECT_EQ(stream_serv()->clients_size(), 1U);
}

// Test ConnectToStream()
TEST_F(StreamCyclerTest, SimpleConfiguration_AutomaticMode_ConnectToStream) {
  // Use Create() in automatic mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 1;
  auto cycler = SetupCyclerStopAtGetNextFrame(false /* manual_mode */, kStreamCount);
  SetupFakeSimpleConfigurations();

  // TEST: GetNextFrame() should be called.
  RunLoopUntilIdle();

  // VERIFY:
  EXPECT_EQ(stream_serv()->clients_size(), 1U);
  EXPECT_EQ(stream_impl(0)->get_next_frame_stat()->call_counter(), 1U);
}

// Test ConnectToStream() (complex configuration)
TEST_F(StreamCyclerTest, ComplexConfiguration_AutomaticMode_ConnectToStream) {
  // Use Create() in automatic mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 5;
  auto cycler = SetupCyclerStopAtGetNextFrame(false /* manual_mode */, kStreamCount);
  SetupFakeComplexConfigurations();

  // TEST: WatchDevices(), WatchCurrentConfigurationCallback() and ConnectToStream() should be
  // called.
  RunLoopUntilIdle();

  // VERIFY:
  EXPECT_EQ(stream_serv()->clients_size(), 3U);
  EXPECT_EQ(stream_impl(0)->get_next_frame_stat()->call_counter(), 1U);
  EXPECT_EQ(stream_impl(1)->get_next_frame_stat()->call_counter(), 1U);
  EXPECT_EQ(stream_impl(2)->get_next_frame_stat()->call_counter(), 1U);
}

// Test WatchCurrentConfigurationCallback()
//
// WatchCurrentConfigurationCallback() is simpler in manual mode and does not daisy-chain to other
// functions. Create() indirectly invokes WatchDevicesCallback(), which should indirectly invoke
// WatchCurrentConfigurationCallback().
TEST_F(StreamCyclerTest, SimpleConfiguration_ManualMode_WatchCurrentConfigurationCallback) {
  // Use Create() in manual mode to construct a StreamCycler.
  auto cycler = SetupCyclerStopAtSync(true /* manual_mode */);
  SetupFakeSimpleConfigurations();

  // TEST: WatchDevices() and WatchCurrentConfigurationCallback() should be called.
  RunLoopUntilIdle();

  // VERIFY:
  EXPECT_EQ(device_watcher_impl()->watch_devices_stat()->call_counter(), 2U);
  EXPECT_EQ(device_watcher_impl()->connect_to_device_stat()->call_counter(), 1U);
  EXPECT_EQ(device_impl()->get_configurations_stat()->call_counter(), 1U);
  EXPECT_EQ(device_impl()->watch_current_configuration_stat()->call_counter(), 2U);
  EXPECT_EQ(device_impl()->watch_mute_state_stat()->call_counter(), 1U);
}

// Test ConnectToStream() (simple configuration)
TEST_F(StreamCyclerTest, SimpleConfiguration_ManualMode_ConnectToStream) {
  // Use Create() in manual mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 1;
  auto cycler = SetupCyclerStopAtGetNextFrame(true /* manual_mode */, kStreamCount);
  SetupFakeSimpleConfigurations();

  // Should run until WatchCurrentConfiguration() because manual mode.
  RunLoopUntilIdle();

  EXPECT_EQ(stream_serv()->clients_size(), 0U);

  // TEST: Force a call to ConnectToStream().
  cycler->ConnectToStream(0 /* config_index */, 0 /* stream_index */);

  // Should be the equivalent of one pass through ConnectToStream.
  RunLoopUntilIdle();

  // VERIFY:
  EXPECT_EQ(stream_serv()->clients_size(), 1U);
  EXPECT_EQ(stream_impl(0)->get_next_frame_stat()->call_counter(), 1U);
}

// Test WatchCurrentConfigurationCallback() (complex configuration)
TEST_F(StreamCyclerTest, ComplexConfiguration_ManualMode_WatchCurrentConfigurationCallback) {
  // Use Create() in manual mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 3;
  auto cycler = SetupCyclerStopAtGetNextFrame(true /* manual_mode */, kStreamCount);
  SetupFakeComplexConfigurations();

  // Should run until WatchCurrentConfiguration() because manual mode.
  RunLoopUntilIdle();

  // Force current_config_index to some incorrect value.
  cycler->current_config_index_ = 25U;

  // TEST: Force a call to WatchCurrentConfigurationCallback()
  cycler->WatchCurrentConfigurationCallback(93);

  // VERIFY:
  EXPECT_EQ(cycler->current_config_index_, 93U);
}

// Test ExecuteSetConfigCommand() (complex configuration)
TEST_F(StreamCyclerTest, ComplexConfiguration_ManualMode_ExecuteSetConfigCommand_SameConfig) {
  // Use Create() in manual mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 3;
  auto cycler = SetupCyclerStopAtGetNextFrame(true /* manual_mode */, kStreamCount);
  SetupFakeComplexConfigurations();
  RunLoopUntilIdle();  // Should run until WatchCurrentConfiguration().

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  cycler->current_config_index_ = 2U;  // Make current_config_index same.

  // TEST: Call SetConfigCommand
  SetupAndInvokeExecuteSetConfigCommand(cycler.get(), 2U /* config_id */);
  RunLoopUntilIdle();  // Should run until end of ExecuteSetConfigCommand().

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // VERIFY:
  // The callback must be called.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 1U);
}

// Test ExecuteSetConfigCommand() (complex configuration)
TEST_F(StreamCyclerTest, ComplexConfiguration_ManualMode_ExecuteSetConfigCommand_DifferentConfig) {
  // Use Create() in manual mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 3;
  auto cycler = SetupCyclerStopAtGetNextFrame(true /* manual_mode */, kStreamCount);
  SetupFakeComplexConfigurations();
  RunLoopUntilIdle();  // Should run until WatchCurrentConfiguration().

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  cycler->current_config_index_ = 1U;  // Make current_config_index different.

  // TEST: Call SetConfigCommand
  SetupAndInvokeExecuteSetConfigCommand(cycler.get(), 2U /* config_id */);
  RunLoopUntilIdle();  // Should run until end of ExecuteSetConfigCommand().

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  // Emulate WatchCurrentConfiguration() callback which occurs because the config id changed.
  cycler->WatchCurrentConfigurationCallback(2U);

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // VERIFY:
  // The callback must be called.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 1U);
}

// Test ExecuteSetAddStreamCommand() (complex configuration)
TEST_F(StreamCyclerTest, ComplexConfiguration_ManualMode_ExecuteAddStreamCommand) {
  // Use Create() in manual mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 3;
  auto cycler = SetupCyclerStopAtGetNextFrame(true /* manual_mode */, kStreamCount);
  SetupFakeComplexConfigurations();
  RunLoopUntilIdle();  // Should run until WatchCurrentConfiguration().

  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  cycler->current_config_index_ = 0U;  // Make current_config_index different.
  SetupAndInvokeExecuteSetConfigCommand(cycler.get(), 2U /* config_id */);
  RunLoopUntilIdle();  // Should run until end of ExecuteSetConfigCommand().

  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  // Emulate WatchCurrentConfiguration() callback which occurs because the config id changed.
  cycler->WatchCurrentConfigurationCallback(2U);

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // The callback must be called.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 1U);

  // TEST: Call ExecuteAddStreamCommand().
  SetupAndInvokeExecuteAddStreamCommand(cycler.get(), 1U /* stream_id */);

  EXPECT_EQ(execute_add_stream_command_stat()->call_counter(), 0U);
  RunLoopUntilIdle();  // Should run until GetNextFrame().

  // The callback should not be called yet.
  EXPECT_EQ(execute_add_stream_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // The callback must be called.
  EXPECT_EQ(execute_add_stream_command_stat()->call_counter(), 1U);

  // VERIFY:
  EXPECT_EQ(on_add_collection_stat()->call_counter(), 1U);
  EXPECT_EQ(on_add_collection_width(), 468.0);
  EXPECT_EQ(on_add_collection_height(), 345.0);
}

// Test ExecuteSetCropCommand() (complex configuration)
TEST_F(StreamCyclerTest, ComplexConfiguration_ManualMode_ExecuteSetCropCommand) {
  // Use Create() in manual mode to construct a StreamCycler.
  constexpr uint32_t kStreamCount = 3;
  auto cycler = SetupCyclerStopAtGetNextFrame(true /* manual_mode */, kStreamCount);
  SetupFakeComplexConfigurations();
  RunLoopUntilIdle();  // Should run until WatchCurrentConfiguration().

  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  cycler->current_config_index_ = 0U;  // Make current_config_index different.
  SetupAndInvokeExecuteSetConfigCommand(cycler.get(), 2U /* config_id */);
  RunLoopUntilIdle();  // Should run until end of ExecuteSetConfigCommand().

  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  // Emulate WatchCurrentConfiguration() callback which occurs because the config id changed.
  cycler->WatchCurrentConfigurationCallback(2U);

  // The callback should not be called yet.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // The callback must be called.
  EXPECT_EQ(execute_set_config_command_stat()->call_counter(), 1U);

  // Call ExecuteAddStreamCommand().
  SetupAndInvokeExecuteAddStreamCommand(cycler.get(), 1U /* stream_id */);

  EXPECT_EQ(execute_add_stream_command_stat()->call_counter(), 0U);
  RunLoopUntilIdle();  // Should run until GetNextFrame().

  // The callback should not be called yet.
  EXPECT_EQ(execute_add_stream_command_stat()->call_counter(), 0U);

  RunControllerLoopUntilIdle();

  // The callback must be called.
  EXPECT_EQ(execute_add_stream_command_stat()->call_counter(), 1U);

  EXPECT_EQ(stream_impl(0)->watch_crop_region_stat()->call_counter(), 0U);
  EXPECT_EQ(stream_impl(0)->set_crop_region_stat()->call_counter(), 0U);
  EXPECT_EQ(execute_set_crop_command_stat()->call_counter(), 0U);

  // OnAddCollection handler should have been called with width & height of config 2 stream 1.
  EXPECT_EQ(on_add_collection_stat()->call_counter(), 1U);
  EXPECT_EQ(on_add_collection_width(), 468.0);
  EXPECT_EQ(on_add_collection_height(), 345.0);

  // TEST: Call ExecuteSetCropCommand().
  SetupAndInvokeExecuteSetCropCommand(cycler.get(),
                                      1U,      // stream_id
                                      77.0,    // x
                                      99.0,    // y
                                      333.0,   // width
                                      222.0);  // height

  RunLoopUntilIdle();  // Should run until SetCropRegion().

  // Emulate WatchCropRegion() callback.
  fuchsia::math::RectF region = {77.0,    // x
                                 99.0,    // y
                                 333.0,   // width
                                 222.0};  // height
  auto region_ptr = std::make_unique<fuchsia::math::RectF>(std::move(region));
  cycler->WatchCropRegionCallback(1U, std::move(region_ptr));

  RunLoopUntilIdle();  // Should run until SetCropRegion().

  // VERIFY:
  EXPECT_EQ(stream_impl(0)->watch_crop_region_stat()->call_counter(), 1U);
  EXPECT_EQ(stream_impl(0)->set_crop_region_stat()->call_counter(), 1U);
}

// TODO(b/180931632) - Need error path unit tests.

}  // namespace camera
