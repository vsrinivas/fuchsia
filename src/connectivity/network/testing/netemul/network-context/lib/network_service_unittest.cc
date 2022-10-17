// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.network/cpp/hlcpp_conversion.h>
#include <fuchsia/netemul/devmgr/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/clock.h>

#include <unordered_set>

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/ethernet_client.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/fake_endpoint.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/netdump.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/netdump_parser.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/network_context.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/realm_setup.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"

namespace {
// Set kTestTimeout to a value other than infinity to test timeouts locally.
constexpr zx::duration kTestTimeout = zx::duration::infinite();
constexpr fuchsia_hardware_network::wire::FrameType kEndpointFrameType =
    static_cast<fuchsia_hardware_network::wire::FrameType>(netemul::Endpoint::kFrameType);
}  // namespace

#define TEST_BUF_SIZE (512ul)
#define WAIT_FOR_OK(ok) ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, kTestTimeout))
#define WAIT_FOR_OK_AND_RESET(ok) \
  WAIT_FOR_OK(ok);                \
  ok = false

namespace netemul {
namespace testing {

static const EthernetConfig TestEthBuffConfig = {.nbufs = 10, .buff_size = 512};

class NetworkServiceTest : public gtest::RealLoopFixture {
 public:
  using FNetworkManager = NetworkManager::FNetworkManager;
  using FEndpointManager = EndpointManager::FEndpointManager;
  using FNetworkContext = NetworkContext::FNetworkContext;
  using FNetwork = Network::FNetwork;
  using FEndpoint = Endpoint::FEndpoint;
  using FFakeEndpoint = FakeEndpoint::FFakeEndpoint;
  using NetworkSetup = NetworkContext::NetworkSetup;
  using EndpointSetup = NetworkContext::EndpointSetup;
  using LossConfig = fuchsia::netemul::network::LossConfig;
  using ReorderConfig = fuchsia::netemul::network::ReorderConfig;
  using LatencyConfig = fuchsia::netemul::network::LatencyConfig;

 protected:
  void SetUp() override {
    real_services_ = sys::ServiceDirectory::CreateFromNamespace();

    svc_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(svc_loop_->StartThread("testloop"));
    svc_ = std::make_unique<NetworkContext>(svc_loop_->dispatcher());

    zx::result result = StartDriverTestRealm();
    ASSERT_OK(result.status_value()) << "driver test realm failed to start";
    devfs_root_.reset(std::move(result.value()));
    svc_->SetDevfsHandler([this](zx::channel req) {
      ASSERT_OK(fidl::WireCall(devfs_root_.directory())
                    ->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights,
                            fidl::ServerEnd<fuchsia_io::Node>(std::move(req)))
                    .status())
          << "failed to connect request to /dev";
    });
    svc_->SetNetworkTunHandler([this](fidl::InterfaceRequest<fuchsia::net::tun::Control> req) {
      real_services_->Connect(std::move(req));
    });
  }

  void GetNetworkManager(fidl::InterfaceRequest<FNetworkManager> nm) {
    fidl::InterfacePtr<FNetworkContext> netc;
    GetNetworkContext(netc.NewRequest());
    netc->GetNetworkManager(std::move(nm));
  }

  void GetEndpointManager(fidl::InterfaceRequest<FEndpointManager> epm) {
    fidl::InterfacePtr<FNetworkContext> netc;
    GetNetworkContext(netc.NewRequest());
    netc->GetEndpointManager(std::move(epm));
  }

  static Endpoint::Config GetDefaultEndpointConfig(
      Endpoint::Backing backing = Endpoint::Backing::ETHERTAP) {
    Endpoint::Config ret;
    ret.mtu = 1500;
    ret.backing = backing;
    return ret;
  }

  void GetServices(fidl::InterfaceRequest<FNetworkManager> nm,
                   fidl::InterfaceRequest<FEndpointManager> epm) {
    fidl::InterfacePtr<FNetworkContext> netc;
    GetNetworkContext(netc.NewRequest());
    netc->GetNetworkManager(std::move(nm));
    netc->GetEndpointManager(std::move(epm));
  }

  void StartServices() { GetServices(net_manager_.NewRequest(), endp_manager_.NewRequest()); }

  void GetNetworkContext(fidl::InterfaceRequest<NetworkContext::FNetworkContext> req) {
    // Always install bindings in the service dispatcher thread.
    async::PostTask(svc_loop_->dispatcher(),
                    [this, req = std::move(req)]() mutable { svc_->GetHandler()(std::move(req)); });
  }

  void CreateNetwork(const char* name, fidl::SynchronousInterfacePtr<FNetwork>* netout,
                     Network::Config config = Network::Config()) {
    ASSERT_TRUE(net_manager_.is_bound());

    zx_status_t status;
    fidl::InterfaceHandle<FNetwork> neth;
    ASSERT_OK(net_manager_->CreateNetwork(name, std::move(config), &status, &neth));
    ASSERT_OK(status);
    ASSERT_TRUE(neth.is_valid());

    *netout = neth.BindSync();
  }

  void CreateEndpoint(const char* name, fidl::SynchronousInterfacePtr<FEndpoint>* netout,
                      Endpoint::Config config) {
    ASSERT_TRUE(net_manager_.is_bound());

    zx_status_t status;
    fidl::InterfaceHandle<FEndpoint> eph;
    ASSERT_OK(endp_manager_->CreateEndpoint(name, std::move(config), &status, &eph));
    ASSERT_OK(status);
    ASSERT_TRUE(eph.is_valid());

    *netout = eph.BindSync();
  }

  void CreateEndpoint(const char* name, fidl::SynchronousInterfacePtr<FEndpoint>* netout,
                      Endpoint::Backing backing = Endpoint::Backing::ETHERTAP) {
    CreateEndpoint(name, netout, GetDefaultEndpointConfig(backing));
  }

  void CreateSimpleNetwork(Network::Config config,
                           fidl::InterfaceHandle<NetworkContext::FSetupHandle>* setup_handle,
                           std::unique_ptr<EthernetClient>* eth1,
                           std::unique_ptr<EthernetClient>* eth2) {
    fidl::SynchronousInterfacePtr<FNetworkContext> context;
    GetNetworkContext(context.NewRequest());
    zx_status_t status;
    std::vector<NetworkSetup> net_setup;
    auto& net1 = net_setup.emplace_back();
    net1.name = "net";
    net1.config = std::move(config);
    auto& ep1_setup = net1.endpoints.emplace_back();
    ep1_setup.name = "ep1";
    ep1_setup.link_up = true;
    auto& ep2_setup = net1.endpoints.emplace_back();
    ep2_setup.name = "ep2";
    ep2_setup.link_up = true;

    ASSERT_OK(context->Setup(std::move(net_setup), &status, setup_handle));
    ASSERT_OK(status);
    fidl::InterfaceHandle<Endpoint::FEndpoint> ep1_handle, ep2_handle;
    ASSERT_OK(endp_manager_->GetEndpoint("ep1", &ep1_handle));
    ASSERT_OK(endp_manager_->GetEndpoint("ep2", &ep2_handle));
    // create both clients
    ASSERT_TRUE(ep1_handle.is_valid());
    ASSERT_TRUE(ep2_handle.is_valid());

    auto ep1 = ep1_handle.BindSync();
    auto ep2 = ep2_handle.BindSync();
    // start ethernet clients on both endpoints:
    fuchsia::netemul::network::DeviceConnection conn1;
    fuchsia::netemul::network::DeviceConnection conn2;
    ASSERT_OK(ep1->GetDevice(&conn1));
    ASSERT_TRUE(conn1.is_ethernet() && conn1.ethernet().is_valid());
    ASSERT_OK(ep2->GetDevice(&conn2));
    ASSERT_TRUE(conn2.is_ethernet() && conn2.ethernet().is_valid());
    *eth1 = std::make_unique<EthernetClient>(dispatcher(), conn1.ethernet().Bind());
    *eth2 = std::make_unique<EthernetClient>(dispatcher(), conn2.ethernet().Bind());

    bool eth_ready = false;
    // configure both ethernet clients:
    (*eth1)->Setup(TestEthBuffConfig, [&eth_ready](zx_status_t status) {
      ASSERT_OK(status);
      eth_ready = true;
    });
    WAIT_FOR_OK_AND_RESET(eth_ready);
    (*eth2)->Setup(TestEthBuffConfig, [&eth_ready](zx_status_t status) {
      ASSERT_OK(status);
      eth_ready = true;
    });
    WAIT_FOR_OK_AND_RESET(eth_ready);

    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&eth1, &eth2]() { return (*eth1)->online() && (*eth2)->online(); }, kTestTimeout));
  }

  zx::result<fuchsia_hardware_network::wire::PortId> GetSinglePortId(
      network::client::NetworkDeviceClient& cli) {
    std::optional<zx::result<fuchsia_hardware_network::wire::PortId>> id;
    cli.GetPorts([&id](zx::result<std::vector<fuchsia_hardware_network::wire::PortId>> result) {
      if (result.is_error()) {
        id = result.take_error();
        return;
      }
      const std::vector<fuchsia_hardware_network::wire::PortId>& port_ids = result.value();
      if (port_ids.size() != 1) {
        ADD_FAILURE() << "observed " << port_ids.size() << "ports, expected a single port";
        id = zx::error(ZX_ERR_INTERNAL);
        return;
      }
      const fuchsia_hardware_network::wire::PortId& port_id = port_ids[0];
      EXPECT_EQ(port_id.base, Endpoint::kPortId);
      id = zx::ok(port_id);
    });
    RunLoopUntil([&id]() { return id.has_value(); });
    return id.value();
  }

  void TearDown() override {
    svc_loop_ = nullptr;
    svc_ = nullptr;
  }

  std::shared_ptr<sys::ServiceDirectory> real_services_;
  fdio_cpp::FdioCaller devfs_root_;
  std::unique_ptr<async::Loop> svc_loop_;
  std::unique_ptr<NetworkContext> svc_;
  fidl::SynchronousInterfacePtr<FNetworkManager> net_manager_;
  fidl::SynchronousInterfacePtr<FEndpointManager> endp_manager_;
};

TEST_F(NetworkServiceTest, NetworkLifecycle) {
  fidl::SynchronousInterfacePtr<FNetworkManager> netm;
  GetNetworkManager(netm.NewRequest());

  const char* netname = "mynet";

  std::vector<std::string> nets;
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(0ul, nets.size());
  Network::Config config;
  zx_status_t status;
  fidl::InterfaceHandle<FNetwork> neth;
  // can create network ok
  ASSERT_OK(netm->CreateNetwork(netname, std::move(config), &status, &neth));
  auto net = neth.BindSync();
  ASSERT_OK(status);
  ASSERT_TRUE(net.is_bound());

  // list nets again and make sure it's there:
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(1ul, nets.size());
  ASSERT_EQ(netname, nets.at(0));

  // check network name matches:
  std::string outname;
  ASSERT_OK(net->GetName(&outname));
  ASSERT_EQ(netname, outname);

  // check that we can fetch the network by name:
  fidl::InterfaceHandle<FNetwork> ohandle;
  ASSERT_OK(netm->GetNetwork(netname, &ohandle));
  ASSERT_TRUE(ohandle.is_valid());
  // dispose of second handle
  ohandle.TakeChannel().reset();

  // check that network still exists:
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(1ul, nets.size());

  // destroy original network handle:
  net.Unbind().TakeChannel().reset();
  // make sure network is deleted afterwards:
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(0ul, nets.size());

  // trying to get the network again without creating it fails:
  ASSERT_OK(netm->GetNetwork(netname, &ohandle));
  ASSERT_FALSE(ohandle.is_valid());
}

TEST_F(NetworkServiceTest, EndpointLifecycle) {
  fidl::SynchronousInterfacePtr<FEndpointManager> epm;
  GetEndpointManager(epm.NewRequest());

  const char* epname = "myendpoint";

  std::vector<std::string> eps;
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(0ul, eps.size());
  auto config = GetDefaultEndpointConfig();
  zx_status_t status;
  fidl::InterfaceHandle<FEndpoint> eph;
  // can create endpoint ok
  ASSERT_OK(epm->CreateEndpoint(epname, std::move(config), &status, &eph));
  auto ep = eph.BindSync();
  ASSERT_OK(status);
  ASSERT_TRUE(ep.is_bound());

  // list endpoints again and make sure it's there:
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(1ul, eps.size());
  ASSERT_EQ(epname, eps.at(0));

  // check endpoint name matches:
  std::string outname;
  ASSERT_OK(ep->GetName(&outname));
  ASSERT_EQ(epname, outname);

  // check that we can fetch the endpoint by name:
  fidl::InterfaceHandle<FEndpoint> ohandle;
  ASSERT_OK(epm->GetEndpoint(epname, &ohandle));
  ASSERT_TRUE(ohandle.is_valid());
  // dispose of second handle
  ohandle.TakeChannel().reset();

  // check that endpoint still exists:
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(1ul, eps.size());

  // destroy original endpoint handle:
  ep.Unbind().TakeChannel().reset();
  // make sure endpoint is deleted afterwards:
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(0ul, eps.size());

  // trying to get the endpoint again without creating it fails:
  ASSERT_OK(epm->GetEndpoint(epname, &ohandle));
  ASSERT_FALSE(ohandle.is_valid());
}

TEST_F(NetworkServiceTest, BadEndpointConfigurations) {
  fidl::SynchronousInterfacePtr<FEndpointManager> epm;
  GetEndpointManager(epm.NewRequest());

  const char* epname = "myendpoint";

  zx_status_t status;
  fidl::InterfaceHandle<FEndpoint> eph;
  // can't create endpoint with empty name
  ASSERT_OK(epm->CreateEndpoint("", GetDefaultEndpointConfig(), &status, &eph));
  ASSERT_STATUS(status, ZX_ERR_INVALID_ARGS);
  ASSERT_FALSE(eph.is_valid());

  // can't create endpoint with unexisting backing
  auto badBacking = GetDefaultEndpointConfig();
  badBacking.backing = static_cast<fuchsia::netemul::network::EndpointBacking>(-1);
  ASSERT_STATUS(epm->CreateEndpoint(epname, std::move(badBacking), &status, &eph),
                ZX_ERR_INVALID_ARGS);
  ASSERT_FALSE(eph.is_valid());

  // can't create endpoint which violates maximum MTU
  auto badMtu = GetDefaultEndpointConfig();
  badMtu.mtu = 65535;  // 65k too large
  ASSERT_OK(epm->CreateEndpoint(epname, std::move(badMtu), &status, &eph));
  ASSERT_STATUS(status, ZX_ERR_INVALID_ARGS);
  ASSERT_FALSE(eph.is_valid());

  // create a good endpoint:
  fidl::InterfaceHandle<FEndpoint> good_eph;
  ASSERT_OK(epm->CreateEndpoint(epname, GetDefaultEndpointConfig(), &status, &good_eph));
  ASSERT_OK(status);
  ASSERT_TRUE(good_eph.is_valid());
  // can't create another endpoint with same name:
  ASSERT_OK(epm->CreateEndpoint(epname, GetDefaultEndpointConfig(), &status, &eph));
  ASSERT_STATUS(status, ZX_ERR_ALREADY_EXISTS);
  ASSERT_FALSE(eph.is_valid());
}

TEST_F(NetworkServiceTest, BadNetworkConfigurations) {
  fidl::SynchronousInterfacePtr<FNetworkManager> netm;
  GetNetworkManager(netm.NewRequest());

  zx_status_t status;
  fidl::InterfaceHandle<FNetwork> neth;
  // can't create network with empty name
  ASSERT_OK(netm->CreateNetwork("", Network::Config(), &status, &neth));
  ASSERT_STATUS(status, ZX_ERR_INVALID_ARGS);
  ASSERT_FALSE(neth.is_valid());

  const char* netname = "mynet";

  // create a good network
  fidl::InterfaceHandle<FNetwork> good_neth;
  ASSERT_OK(netm->CreateNetwork(netname, Network::Config(), &status, &good_neth));
  ASSERT_OK(status);
  ASSERT_TRUE(good_neth.is_valid());

  // can't create another network with same name:
  ASSERT_OK(netm->CreateNetwork(netname, Network::Config(), &status, &neth));
  ASSERT_STATUS(status, ZX_ERR_ALREADY_EXISTS);
  ASSERT_FALSE(neth.is_valid());
}

TEST_F(NetworkServiceTest, TransitData) {
  const char* netname = "mynet";
  const char* ep1name = "ep1";
  const char* ep2name = "ep2";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create first endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(ep1name, &ep1);

  // create second endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep2;
  CreateEndpoint(ep2name, &ep2);
  ASSERT_OK(ep1->SetLinkUp(true));
  ASSERT_OK(ep2->SetLinkUp(true));

  // attach both endpoints:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(ep1name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(ep2name, &status));
  ASSERT_OK(status);

  // start ethernet clients on both endpoints:
  fuchsia::netemul::network::DeviceConnection conn1;
  fuchsia::netemul::network::DeviceConnection conn2;
  ASSERT_OK(ep1->GetDevice(&conn1));
  ASSERT_TRUE(conn1.is_ethernet() && conn1.ethernet().is_valid());
  ASSERT_OK(ep2->GetDevice(&conn2));
  ASSERT_TRUE(conn2.is_ethernet() && conn2.ethernet().is_valid());
  // create both clients
  EthernetClient eth1(dispatcher(), conn1.ethernet().Bind());
  EthernetClient eth2(dispatcher(), conn2.ethernet().Bind());
  bool ok = false;

  // configure both ethernet clients:
  eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  eth2.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  // wait for both ethernets to come online
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&eth1, &eth2]() { return eth1.online() && eth2.online(); },
                                        kTestTimeout));

  // create some test buffs
  uint8_t test_buff1[TEST_BUF_SIZE];
  uint8_t test_buff2[TEST_BUF_SIZE];
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff1[i] = static_cast<uint8_t>(i);
    test_buff2[i] = ~static_cast<uint8_t>(i);
  }

  // install callbacks on the ethernet interfaces:
  eth1.SetDataCallback([&ok, &test_buff1](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff1, len));
    ok = true;
  });
  eth2.SetDataCallback([&ok, &test_buff2](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff2, len));
    ok = true;
  });

  // send data from eth2 to eth1
  ASSERT_OK(eth2.Send(test_buff1, TEST_BUF_SIZE));
  WAIT_FOR_OK_AND_RESET(ok);

  // send data from eth1 to eth2
  ASSERT_OK(eth1.Send(test_buff2, TEST_BUF_SIZE));
  WAIT_FOR_OK_AND_RESET(ok);

  // try removing an endpoint:
  ASSERT_OK(net->RemoveEndpoint(ep2name, &status));
  ASSERT_OK(status);
  // can still send, but should not trigger anything on the other side:
  ASSERT_OK(eth1.Send(test_buff1, TEST_BUF_SIZE));
  RunLoopUntilIdle();
  ASSERT_FALSE(ok);
}

TEST_F(NetworkServiceTest, Flooding) {
  const char* netname = "mynet";
  const char* ep1name = "ep1";
  const char* ep2name = "ep2";
  const char* ep3name = "ep3";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create first endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(ep1name, &ep1);
  // create second endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep2;
  CreateEndpoint(ep2name, &ep2);
  // create a third:
  fidl::SynchronousInterfacePtr<FEndpoint> ep3;
  CreateEndpoint(ep3name, &ep3);
  ASSERT_OK(ep1->SetLinkUp(true));
  ASSERT_OK(ep2->SetLinkUp(true));
  ASSERT_OK(ep3->SetLinkUp(true));

  // attach all three endpoints:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(ep1name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(ep2name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(ep3name, &status));
  ASSERT_OK(status);

  // start ethernet clients on all endpoints:
  fuchsia::netemul::network::DeviceConnection conn1;
  fuchsia::netemul::network::DeviceConnection conn2;
  fuchsia::netemul::network::DeviceConnection conn3;
  ASSERT_OK(ep1->GetDevice(&conn1));
  ASSERT_TRUE(conn1.is_ethernet() && conn1.ethernet().is_valid());
  ASSERT_OK(ep2->GetDevice(&conn2));
  ASSERT_TRUE(conn2.is_ethernet() && conn2.ethernet().is_valid());
  ASSERT_OK(ep3->GetDevice(&conn3));
  ASSERT_TRUE(conn3.is_ethernet() && conn3.ethernet().is_valid());
  // create all ethernet clients
  EthernetClient eth1(dispatcher(), conn1.ethernet().Bind());
  EthernetClient eth2(dispatcher(), conn2.ethernet().Bind());
  EthernetClient eth3(dispatcher(), conn3.ethernet().Bind());
  bool ok = false;

  // configure all ethernet clients:
  eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  eth2.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  eth3.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  // Wait for all ethernets to come online
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&eth1, &eth2, &eth3]() { return eth1.online() && eth2.online() && eth3.online(); },
      kTestTimeout));

  // create a test buff
  uint8_t test_buff[TEST_BUF_SIZE];
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff[i] = static_cast<uint8_t>(i);
  }

  // install callbacks on the ethernet interfaces:
  bool ok_eth1 = false;
  bool ok_eth2 = false;
  bool ok_eth3 = false;
  eth1.SetDataCallback([&ok_eth1, &test_buff](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff, len));
    ok_eth1 = true;
  });
  eth2.SetDataCallback([&ok_eth2, &test_buff](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff, len));
    ok_eth2 = true;
  });
  eth3.SetDataCallback([&ok_eth3, &test_buff](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff, len));
    ok_eth3 = true;
  });

  for (int i = 0; i < 3; i++) {
    // flood network from eth1:
    ASSERT_OK(eth1.Send(test_buff, TEST_BUF_SIZE));
    // wait for corrrect data on both endpoints:
    WAIT_FOR_OK_AND_RESET(ok_eth2);
    WAIT_FOR_OK_AND_RESET(ok_eth3);
    // eth1 should have received NO data at this point:
    ASSERT_FALSE(ok_eth1);
    // now flood from eth2:
    ASSERT_OK(eth2.Send(test_buff, TEST_BUF_SIZE));
    // wait for corrrect data on both endpoints:
    WAIT_FOR_OK_AND_RESET(ok_eth1);
    WAIT_FOR_OK_AND_RESET(ok_eth3);
    ASSERT_FALSE(ok_eth2);
  }
}

TEST_F(NetworkServiceTest, AttachRemove) {
  const char* netname = "mynet";
  const char* epname = "ep1";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create an endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(epname, &ep1);

  // attach endpoint:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(epname, &status));
  ASSERT_OK(status);
  // try to attach again:
  ASSERT_OK(net->AttachEndpoint(epname, &status));
  // should return error because endpoint was already attached
  ASSERT_STATUS(status, ZX_ERR_ALREADY_BOUND);

  // remove endpoint:
  ASSERT_OK(net->RemoveEndpoint(epname, &status));
  ASSERT_OK(status);
  // remove endpoint again:
  ASSERT_OK(net->RemoveEndpoint(epname, &status));
  // should return error because endpoint was not attached
  ASSERT_STATUS(status, ZX_ERR_NOT_FOUND);
}

TEST_F(NetworkServiceTest, FakeEndpoints) {
  const char* netname = "mynet";
  const char* epname = "ep1";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create first endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(epname, &ep1);
  ep1->SetLinkUp(true);

  // attach endpoint:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(epname, &status));
  ASSERT_OK(status);

  // start ethernet clients on endpoint:
  fuchsia::netemul::network::DeviceConnection conn1;
  ASSERT_OK(ep1->GetDevice(&conn1));
  ASSERT_TRUE(conn1.is_ethernet() && conn1.ethernet().is_valid());
  // create client
  EthernetClient eth1(dispatcher(), conn1.ethernet().Bind());
  bool ok = false;

  // configure ethernet client:
  eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  // and wait for it to come online:
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&eth1]() { return eth1.online(); }, kTestTimeout));

  // create some test buffs
  std::vector<uint8_t> test_buff1(TEST_BUF_SIZE);
  std::vector<uint8_t> test_buff2(TEST_BUF_SIZE);
  test_buff2.reserve(TEST_BUF_SIZE);
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff1[i] = static_cast<uint8_t>(i);
    test_buff2[i] = ~static_cast<uint8_t>(i);
  }

  // install callbacks on the ethernet interface:
  eth1.SetDataCallback([&ok, &test_buff1](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff1.data(), len));
    ok = true;
  });

  // create and inject a fake endpoint:
  fidl::InterfacePtr<FFakeEndpoint> fake_ep;
  ASSERT_OK(net->CreateFakeEndpoint(fake_ep.NewRequest()));
  ASSERT_TRUE(fake_ep.is_bound());

  for (int i = 0; i < 3; i++) {
    // send buff 2 from eth endpoint:
    eth1.Send(test_buff2.data(), static_cast<uint16_t>(test_buff2.size()));
    // Read the next frame.
    fake_ep->Read([&ok, &test_buff2](std::vector<uint8_t> data, uint64_t dropped) {
      EXPECT_EQ(dropped, 0u);
      ASSERT_EQ(TEST_BUF_SIZE, data.size());
      ASSERT_EQ(0, memcmp(data.data(), test_buff2.data(), data.size()));
      ok = true;
    });
    WAIT_FOR_OK_AND_RESET(ok);
    // send buff 1 from fake endpoint:
    fake_ep->Write(test_buff1, []() {});
    WAIT_FOR_OK_AND_RESET(ok);
  }
}

TEST_F(NetworkServiceTest, FakeEndpointsDropFrames) {
  const char* netname = "mynet";
  StartServices();

  // Create a network.
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // Create a pair of fake endpoints.
  fidl::SynchronousInterfacePtr<FFakeEndpoint> fake_ep_1;
  ASSERT_OK(net->CreateFakeEndpoint(fake_ep_1.NewRequest()));
  ASSERT_TRUE(fake_ep_1.is_bound());
  fidl::SynchronousInterfacePtr<FFakeEndpoint> fake_ep_2;
  ASSERT_OK(net->CreateFakeEndpoint(fake_ep_2.NewRequest()));
  ASSERT_TRUE(fake_ep_2.is_bound());

  constexpr uint64_t kDropCount = 10;

  // Write something on the EP we'll be doing the read on to make sure it's installed because
  // CreateFakeEndpoint is pipelined.
  ASSERT_OK(fake_ep_2->Write({0xAA}));

  for (uint64_t i = 0; i < FakeEndpoint::kMaxPendingFrames + kDropCount; i++) {
    ASSERT_OK(fake_ep_1->Write({static_cast<uint8_t>(i), 2, 3}));
  }
  std::vector<uint8_t> o_data;
  uint64_t o_dropped;
  ASSERT_OK(fake_ep_2->Read(&o_data, &o_dropped));
  // Check that the expected number of frames was dropped.
  ASSERT_EQ(o_dropped, kDropCount);
}

TEST_F(NetworkServiceTest, FakeEndpointDisallowsMultipleReads) {
  const char* netname = "mynet";
  StartServices();

  // Create a network.
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // Create a fake endpoint.
  fidl::InterfacePtr<FFakeEndpoint> fake_ep;
  ASSERT_OK(net->CreateFakeEndpoint(fake_ep.NewRequest()));
  ASSERT_TRUE(fake_ep.is_bound());
  bool errored = false;
  fake_ep.set_error_handler([&errored](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_BAD_STATE);
    errored = true;
  });
  // Read twice and expect the error to occur.
  for (int i = 0; i < 2; i++) {
    fake_ep->Read([](std::vector<uint8_t> data, uint64_t dropped) {
      FAIL() << "There should be no data to read";
    });
  }
  WAIT_FOR_OK(errored);
}

TEST_F(NetworkServiceTest, NetworkContext) {
  StartServices();
  fidl::SynchronousInterfacePtr<FNetworkContext> context;
  GetNetworkContext(context.NewRequest());

  zx_status_t status;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> setup_handle;
  std::vector<NetworkSetup> net_setup;
  auto& net1 = net_setup.emplace_back();
  net1.name = "main_net";
  auto& ep1_setup = net1.endpoints.emplace_back();
  ep1_setup.name = "ep1";
  ep1_setup.link_up = true;
  auto& ep2_setup = net1.endpoints.emplace_back();
  ep2_setup.name = "ep2";
  ep2_setup.link_up = true;
  auto& alt_net_setup = net_setup.emplace_back();
  alt_net_setup.name = "alt_net";

  // create two nets and two endpoints:
  ASSERT_OK(context->Setup(std::move(net_setup), &status, &setup_handle));
  ASSERT_OK(status);
  ASSERT_TRUE(setup_handle.is_valid());

  // check that both networks and endpoints were created:
  fidl::InterfaceHandle<Network::FNetwork> network;
  ASSERT_OK(net_manager_->GetNetwork("main_net", &network));
  ASSERT_TRUE(network.is_valid());
  ASSERT_OK(net_manager_->GetNetwork("alt_net", &network));
  ASSERT_TRUE(network.is_valid());
  fidl::InterfaceHandle<Endpoint::FEndpoint> ep1_h, ep2_h;
  ASSERT_OK(endp_manager_->GetEndpoint("ep1", &ep1_h));
  ASSERT_TRUE(ep1_h.is_valid());
  ASSERT_OK(endp_manager_->GetEndpoint("ep2", &ep2_h));
  ASSERT_TRUE(ep2_h.is_valid());

  {
    // check that endpoints were attached to the same network:
    auto ep1 = ep1_h.BindSync();
    auto ep2 = ep2_h.BindSync();
    // start ethernet clients on both endpoints:
    fuchsia::netemul::network::DeviceConnection conn1;
    fuchsia::netemul::network::DeviceConnection conn2;
    ASSERT_OK(ep1->GetDevice(&conn1));
    ASSERT_TRUE(conn1.is_ethernet() && conn1.ethernet().is_valid());
    ASSERT_OK(ep2->GetDevice(&conn2));
    ASSERT_TRUE(conn2.is_ethernet() && conn2.ethernet().is_valid());
    // create both clients
    EthernetClient eth1(dispatcher(), conn1.ethernet().Bind());
    EthernetClient eth2(dispatcher(), conn2.ethernet().Bind());
    bool ok = false;

    // configure both ethernet clients:
    eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
      ASSERT_OK(status);
      ok = true;
    });
    WAIT_FOR_OK_AND_RESET(ok);
    eth2.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
      ASSERT_OK(status);
      ok = true;
    });
    WAIT_FOR_OK_AND_RESET(ok);
    // and wait for them to come online:
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&eth1, &eth2]() { return eth1.online() && eth2.online(); }, kTestTimeout));

    // create some test buffs
    uint8_t test_buff[TEST_BUF_SIZE];
    for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
      test_buff[i] = static_cast<uint8_t>(i);
    }
    // install callbacks on the ethernet interface:
    eth2.SetDataCallback([&ok, &test_buff](const void* data, size_t len) {
      ASSERT_EQ(TEST_BUF_SIZE, len);
      ASSERT_EQ(0, memcmp(data, test_buff, len));
      ok = true;
    });
    ASSERT_OK(eth1.Send(test_buff, TEST_BUF_SIZE));
    WAIT_FOR_OK_AND_RESET(ok);
  }  // test above performed in closed scope so all bindings are destroyed after
  // it's done

  // check that attempting to set up with repeated network name will fail:
  std::vector<NetworkSetup> repeated_net_name;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> dummy_handle;
  auto& repeated_cfg = repeated_net_name.emplace_back();
  repeated_cfg.name = "main_net";
  ASSERT_OK(context->Setup(std::move(repeated_net_name), &status, &dummy_handle));
  ASSERT_STATUS(status, ZX_ERR_ALREADY_EXISTS);
  ASSERT_FALSE(dummy_handle.is_valid());

  // check that attempting to set up with invalid ep name (ep1 already exists) will fail, and all
  // setup is discarded
  std::vector<NetworkSetup> repeated_ep_name;
  auto& good_net = repeated_ep_name.emplace_back();
  good_net.name = "good_net";
  auto& repeated_ep1_setup = good_net.endpoints.emplace_back();
  repeated_ep1_setup.name = "ep1";

  ASSERT_OK(context->Setup(std::move(repeated_ep_name), &status, &dummy_handle));
  ASSERT_STATUS(status, ZX_ERR_ALREADY_EXISTS);
  ASSERT_FALSE(dummy_handle.is_valid());
  ASSERT_OK(net_manager_->GetNetwork("good_net", &network));
  ASSERT_FALSE(network.is_valid());

  // finally, destroy the setup_handle and verify that all the created networks
  // and endpoints go away:
  setup_handle.TakeChannel().reset();

  // wait until |main_net| disappears:
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [this]() {
        fidl::InterfaceHandle<Network::FNetwork> network;
        EXPECT_OK(net_manager_->GetNetwork("main_net", &network));
        return !network.is_valid();
      },
      kTestTimeout));
  // assert that all other networks and endpoints also disappear:
  ASSERT_OK(net_manager_->GetNetwork("alt_net", &network));
  ASSERT_FALSE(network.is_valid());
  ASSERT_OK(endp_manager_->GetEndpoint("ep1", &ep1_h));
  ASSERT_FALSE(ep1_h.is_valid());
  ASSERT_OK(endp_manager_->GetEndpoint("ep2", &ep2_h));
  ASSERT_FALSE(ep2_h.is_valid());
}

TEST_F(NetworkServiceTest, CreateNetworkWithInvalidConfig) {
  StartServices();
  Network::Config config;
  LossConfig loss;
  loss.set_random_rate(101);
  config.set_packet_loss(std::move(loss));
  zx_status_t status;
  fidl::InterfaceHandle<Network::FNetwork> net;
  ASSERT_OK(net_manager_->CreateNetwork("net", std::move(config), &status, &net));
  ASSERT_STATUS(status, ZX_ERR_INVALID_ARGS);
  ASSERT_FALSE(net.is_valid());
}

TEST_F(NetworkServiceTest, NetworkSetInvalidConfig) {
  StartServices();
  fidl::SynchronousInterfacePtr<Network::FNetwork> net;
  CreateNetwork("net", &net);

  Network::Config config;
  LossConfig loss;
  loss.set_random_rate(101);
  config.set_packet_loss(std::move(loss));
  zx_status_t status;
  ASSERT_OK(net->SetConfig(std::move(config), &status));
  ASSERT_STATUS(status, ZX_ERR_INVALID_ARGS);
}

TEST_F(NetworkServiceTest, NetworkConfigChains) {
  StartServices();
  constexpr int packet_count = 3;
  std::unique_ptr<EthernetClient> eth1, eth2;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> setup_handle;
  Network::Config config;
  config.mutable_packet_loss()->set_random_rate(0);
  config.mutable_latency()->average = 5;
  config.mutable_latency()->std_dev = 0;
  config.mutable_reorder()->store_buff = packet_count;
  config.mutable_reorder()->tick = 0;

  CreateSimpleNetwork(std::move(config), &setup_handle, &eth1, &eth2);
  ASSERT_TRUE(eth1 && eth2);

  std::unordered_set<uint8_t> received;
  zx::time after;
  eth2->SetDataCallback([&received, &after](const void* data, size_t len) {
    EXPECT_EQ(len, 1ul);
    received.insert(*reinterpret_cast<const uint8_t*>(data));
    after = zx::clock::get_monotonic();
  });

  auto bef = zx::clock::get_monotonic();
  for (uint8_t i = 0; i < packet_count; i++) {
    ASSERT_OK(eth1->Send(&i, 1));
  }

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&received]() { return received.size() == packet_count; },
                                        kTestTimeout));
  for (uint8_t i = 0; i < packet_count; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }
  auto diff = (after - bef).to_msecs();
  // Check that measured latency is at least greater than the configured
  // one.
  // We don't do upper bound checking because it's not very CQ friendly.
  EXPECT_TRUE(diff >= 5) << "Total latency should be greater than configured latency, but got "
                         << diff;
}

TEST_F(NetworkServiceTest, NetworkConfigChanges) {
  StartServices();
  constexpr int reorder_threshold = 3;
  constexpr int packet_count = 5;
  std::unique_ptr<EthernetClient> eth1, eth2;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> setup_handle;
  Network::Config config;
  config.mutable_reorder()->store_buff = reorder_threshold;
  config.mutable_reorder()->tick = 0;

  // start with creating a network with a reorder threshold lower than the sent
  // packet count
  CreateSimpleNetwork(std::move(config), &setup_handle, &eth1, &eth2);
  ASSERT_TRUE(eth1 && eth2 && setup_handle.is_valid());

  std::unordered_set<uint8_t> received;
  eth2->SetDataCallback([&received](const void* data, size_t len) {
    EXPECT_EQ(len, 1ul);
    received.insert(*reinterpret_cast<const uint8_t*>(data));
  });

  for (uint8_t i = 0; i < packet_count; i++) {
    ASSERT_OK(eth1->Send(&i, 1));
  }

  // wait until |reorder_threshold| is hit
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&received]() { return received.size() == reorder_threshold; }, kTestTimeout));
  for (uint8_t i = 0; i < reorder_threshold; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }
  received.clear();

  // change the configuration to packet loss 0:
  Network::Config config_packet_loss;
  config_packet_loss.mutable_packet_loss()->set_random_rate(0);
  fidl::InterfaceHandle<Network::FNetwork> net_handle;
  ASSERT_OK(net_manager_->GetNetwork("net", &net_handle));
  ASSERT_TRUE(net_handle.is_valid());
  auto net = net_handle.BindSync();
  zx_status_t status;
  ASSERT_OK(net->SetConfig(std::move(config_packet_loss), &status));
  ASSERT_OK(status);
  // upon changing the configuration, all other remaining packets should've been
  // flushed check that by waiting for the remaining packets: wait until
  // |reorder_threshold| is hit
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&received]() { return received.size() == packet_count - reorder_threshold; }, kTestTimeout));
  for (uint8_t i = reorder_threshold; i < packet_count; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }

  received.clear();
  // go again to verify that the configuration changed into packet loss with 0%
  // loss:
  for (uint8_t i = 0; i < packet_count; i++) {
    ASSERT_OK(eth1->Send(&i, 1));
  }
  // wait until |packet_count| is hit
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&received]() { return received.size() == packet_count; },
                                        kTestTimeout));
  for (uint8_t i = 0; i < packet_count; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }
  received.clear();
}

TEST_F(NetworkServiceTest, NetWatcher) {
  StartServices();

  fidl::SynchronousInterfacePtr<Network::FNetwork> net;
  CreateNetwork("net", &net);

  fidl::InterfacePtr<FakeEndpoint::FFakeEndpoint> fe;
  ASSERT_OK(net->CreateFakeEndpoint(fe.NewRequest()));
  // create net watcher first, so we're guaranteed it'll be there before:
  NetWatcher<InMemoryDump> watcher;
  watcher.Watch("net", std::move(fe));

  fidl::InterfacePtr<FakeEndpoint::FFakeEndpoint> fe_in;
  ASSERT_OK(net->CreateFakeEndpoint(fe_in.NewRequest()));

  constexpr uint32_t packet_count = 10;
  for (uint32_t i = 0; i < packet_count; i++) {
    auto* ptr = reinterpret_cast<const uint8_t*>(&i);
    fe_in->Write(std::vector<uint8_t>(ptr, ptr + sizeof(i)), []() {});
  }

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&watcher]() { return watcher.dump().packet_count() == packet_count; }, kTestTimeout));

  // check that all the saved data is correct:
  NetDumpParser parser;
  auto dump_bytes = watcher.dump().CopyBytes();
  ASSERT_TRUE(parser.Parse(dump_bytes.data(), dump_bytes.size()));
  ASSERT_EQ(parser.interfaces().size(), 1ul);
  ASSERT_EQ(parser.packets().size(), packet_count);

  EXPECT_EQ(parser.interfaces()[0], "net");
  for (uint32_t i = 0; i < packet_count; i++) {
    auto& pkt = parser.packets()[i];
    EXPECT_EQ(pkt.len, sizeof(uint32_t));
    EXPECT_EQ(pkt.interface, 0u);
    EXPECT_EQ(memcmp(pkt.data, &i, sizeof(i)), 0);
  }
}

// Tests creating a NetworkDevice and an Ethertap device and that they can communicate over a
// network.
TEST_F(NetworkServiceTest, HybridNetworkDevice) {
  const char* netname = "mynet";
  const char* eth_ep_name = "ep-eth";
  const char* netdev_ep_name = "ep-netdev";
  StartServices();

  // Create a network.
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // Create first endpoint.
  fidl::SynchronousInterfacePtr<FEndpoint> eth_ep;
  CreateEndpoint(eth_ep_name, &eth_ep, Endpoint::Backing::ETHERTAP);

  // Create second endpoint.
  fidl::SynchronousInterfacePtr<FEndpoint> netdev_ep;
  CreateEndpoint(netdev_ep_name, &netdev_ep, Endpoint::Backing::NETWORK_DEVICE);
  ASSERT_OK(eth_ep->SetLinkUp(true));
  ASSERT_OK(netdev_ep->SetLinkUp(true));

  // Attach both endpoints.
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(eth_ep_name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(netdev_ep_name, &status));
  ASSERT_OK(status);

  // Start ethernet clients on both endpoints.
  fuchsia::netemul::network::DeviceConnection conn_eth;
  fuchsia::netemul::network::DeviceConnection conn_netdev;
  ASSERT_OK(eth_ep->GetDevice(&conn_eth));
  ASSERT_TRUE(conn_eth.is_ethernet() && conn_eth.ethernet().is_valid());
  ASSERT_OK(netdev_ep->GetDevice(&conn_netdev));
  ASSERT_TRUE(conn_netdev.is_network_device());
  ASSERT_TRUE(conn_netdev.network_device().is_valid());
  // Create both clients.
  EthernetClient eth_cli(dispatcher(), conn_eth.ethernet().Bind());
  fidl::InterfaceHandle<fuchsia::hardware::network::Device> device;
  conn_netdev.network_device().Bind()->GetDevice(device.NewRequest());
  network::client::NetworkDeviceClient netdev_cli(fidl::HLCPPToNatural(device));
  bool ok = false;

  // Configure both ethernet clients.
  eth_cli.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  netdev_cli.OpenSession("test_session", [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);

  zx::result maybe_port_id = GetSinglePortId(netdev_cli);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  netdev_cli.AttachPort(port_id, {kEndpointFrameType}, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);

  // Wait for both to come online.
  {
    bool netdev_online = false;
    auto watcher = netdev_cli.WatchStatus(
        port_id, [&netdev_online](fuchsia_hardware_network::wire::PortStatus status) {
          if (status.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline) {
            netdev_online = true;
          }
        });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&eth_cli, &netdev_online]() { return eth_cli.online() && netdev_online; }, kTestTimeout))
        << "Test timed out, eth_cli online=" << eth_cli.online()
        << ", netdev_online=" << netdev_online;
  }

  // Create some test buffs.
  uint8_t test_buff1[TEST_BUF_SIZE];
  uint8_t test_buff2[TEST_BUF_SIZE];
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff1[i] = static_cast<uint8_t>(i);
    test_buff2[i] = ~static_cast<uint8_t>(i);
  }

  // Install callbacks on the ethernet interfaces.
  bool rx_eth = false;
  bool rx_netdev = false;
  eth_cli.SetDataCallback([&rx_eth, &test_buff1](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff1, len));
    rx_eth = true;
  });

  netdev_cli.SetRxCallback(
      [&rx_netdev, &test_buff2](network::client::NetworkDeviceClient::Buffer buff) {
        ASSERT_EQ(TEST_BUF_SIZE, buff.data().len());
        ASSERT_EQ(buff.data().parts(), 1u);
        ASSERT_EQ(0, memcmp(buff.data().part(0).data().data(), test_buff2, TEST_BUF_SIZE));
        rx_netdev = true;
      });

  // Send data from netdev to eth.
  auto tx = netdev_cli.AllocTx();
  ASSERT_TRUE(tx.is_valid());
  tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
  tx.data().SetPortId(port_id);
  ASSERT_EQ(tx.data().Write(test_buff1, TEST_BUF_SIZE), TEST_BUF_SIZE);
  ASSERT_OK(tx.Send());
  WAIT_FOR_OK_AND_RESET(rx_eth);

  // Send data from eth to netdev.
  ASSERT_OK(eth_cli.Send(test_buff2, TEST_BUF_SIZE));
  WAIT_FOR_OK_AND_RESET(rx_netdev);

  // Try removing an endpoint.
  ASSERT_OK(net->RemoveEndpoint(netdev_ep_name, &status));
  ASSERT_OK(status);
  // Can still send, but should not trigger anything on the other side.
  ASSERT_OK(eth_cli.Send(test_buff1, TEST_BUF_SIZE));
  RunLoopUntilIdle();
  ASSERT_FALSE(rx_netdev) << "Unexpectedly triggered netdev_cli callback";
}

TEST_F(NetworkServiceTest, DualNetworkDevice) {
  const char* netname = "mynet";
  const char* ep1name = "ep1";
  const char* ep2name = "ep2";
  StartServices();

  // Create a network.
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // Create first endpoint.
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(ep1name, &ep1, Endpoint::Backing::NETWORK_DEVICE);

  // Create second endpoint.
  fidl::SynchronousInterfacePtr<FEndpoint> ep2;
  CreateEndpoint(ep2name, &ep2, Endpoint::Backing::NETWORK_DEVICE);
  ASSERT_OK(ep1->SetLinkUp(true));
  ASSERT_OK(ep2->SetLinkUp(true));

  // Attach both endpoints.
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(ep1name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(ep2name, &status));
  ASSERT_OK(status);

  // Start ethernet clients on both endpoints.
  fuchsia::netemul::network::DeviceConnection conn1;
  fuchsia::netemul::network::DeviceConnection conn2;
  ASSERT_OK(ep1->GetDevice(&conn1));
  ASSERT_TRUE(conn1.is_network_device() && conn1.network_device().is_valid());
  ASSERT_OK(ep2->GetDevice(&conn2));
  ASSERT_TRUE(conn2.is_network_device() && conn2.network_device().is_valid());
  // Create both clients.
  fidl::InterfaceHandle<fuchsia::hardware::network::Device> device1;
  conn1.network_device().Bind()->GetDevice(device1.NewRequest());
  network::client::NetworkDeviceClient cli1(fidl::HLCPPToNatural(device1));
  fidl::InterfaceHandle<fuchsia::hardware::network::Device> device2;
  conn2.network_device().Bind()->GetDevice(device2.NewRequest());
  network::client::NetworkDeviceClient cli2(fidl::HLCPPToNatural(device2));
  bool ok = false;

  // Configure both ethernet clients.
  cli1.OpenSession("test_session1", [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  cli2.OpenSession("test_session2", [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);

  zx::result maybe_port_id = GetSinglePortId(cli1);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id1 = maybe_port_id.value();
  cli1.AttachPort(port_id1, {kEndpointFrameType}, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);

  maybe_port_id = GetSinglePortId(cli2);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id2 = maybe_port_id.value();
  cli2.AttachPort(port_id2, {kEndpointFrameType}, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);

  // Wait for both to come online.
  {
    bool online1 = false;
    bool online2 = false;
    auto watcher1 =
        cli1.WatchStatus(port_id1, [&online1](fuchsia_hardware_network::wire::PortStatus status) {
          if (status.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline) {
            online1 = true;
          }
        });
    auto watcher2 =
        cli2.WatchStatus(port_id2, [&online2](fuchsia_hardware_network::wire::PortStatus status) {
          if (status.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline) {
            online2 = true;
          }
        });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&online1, &online2]() { return online1 && online2; },
                                          kTestTimeout));
  }

  // Create some test buffs.
  uint8_t test_buff1[TEST_BUF_SIZE];
  uint8_t test_buff2[TEST_BUF_SIZE];
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff1[i] = static_cast<uint8_t>(i);
    test_buff2[i] = ~static_cast<uint8_t>(i);
  }

  // Install callbacks on the clients.
  bool rx1 = false;
  bool rx2 = false;
  cli1.SetRxCallback([&rx1, &test_buff1](network::client::NetworkDeviceClient::Buffer buff) {
    ASSERT_EQ(TEST_BUF_SIZE, buff.data().len());
    ASSERT_EQ(buff.data().parts(), 1u);
    ASSERT_EQ(0, memcmp(buff.data().part(0).data().data(), test_buff1, TEST_BUF_SIZE));
    rx1 = true;
  });

  cli2.SetRxCallback([&rx2, &test_buff2](network::client::NetworkDeviceClient::Buffer buff) {
    ASSERT_EQ(TEST_BUF_SIZE, buff.data().len());
    ASSERT_EQ(buff.data().parts(), 1u);
    ASSERT_EQ(0, memcmp(buff.data().part(0).data().data(), test_buff2, TEST_BUF_SIZE));
    rx2 = true;
  });

  // Send data from cli2 to cli1.
  {
    auto tx = cli2.AllocTx();
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    tx.data().SetPortId(port_id2);
    ASSERT_EQ(tx.data().Write(test_buff1, TEST_BUF_SIZE), TEST_BUF_SIZE);
    ASSERT_OK(tx.Send());
    WAIT_FOR_OK_AND_RESET(rx1);
  }

  // Send data from cli1 to cli2.
  {
    auto tx = cli1.AllocTx();
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    tx.data().SetPortId(port_id1);
    ASSERT_EQ(tx.data().Write(test_buff2, TEST_BUF_SIZE), TEST_BUF_SIZE);
    ASSERT_OK(tx.Send());
    WAIT_FOR_OK_AND_RESET(rx2);
  }

  // Try removing an endpoint.
  ASSERT_OK(net->RemoveEndpoint(ep2name, &status));
  ASSERT_OK(status);
  // Can still send, but should not trigger anything on the other side.
  {
    auto tx = cli1.AllocTx();
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    tx.data().SetPortId(port_id1);
    ASSERT_EQ(tx.data().Write(test_buff2, TEST_BUF_SIZE), TEST_BUF_SIZE);
    ASSERT_OK(tx.Send());
  }
  RunLoopUntilIdle();
  ASSERT_FALSE(rx2) << "Unexpectedly triggered cli2 data callback";
}

TEST_F(NetworkServiceTest, VirtualizationTeardown) {
  constexpr char netname[] = "mynet";
  StartServices();

  // Create a network.
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  fidl::SynchronousInterfacePtr<fuchsia::net::tun::Control> tun_ctl =
      svc_->ConnectNetworkTun().BindSync();
  fidl::SynchronousInterfacePtr<fuchsia::net::tun::Device> tun_device;
  ASSERT_OK(tun_ctl->CreateDevice({}, tun_device.NewRequest()));

  fidl::InterfacePtr<fuchsia::net::virtualization::Interface> virtualization_interface;
  std::optional<zx_status_t> virtualization_interface_status;
  virtualization_interface.set_error_handler(
      [&virtualization_interface_status](zx_status_t status) {
        virtualization_interface_status = status;
      });

  // Attempt to attach to a closed port.
  {
    fidl::InterfaceHandle<fuchsia::hardware::network::Port> port_handle;
    fidl::InterfaceRequest port_request = port_handle.NewRequest();
    port_request.TakeChannel().reset();
    ASSERT_OK(net->AddPort(std::move(port_handle), virtualization_interface.NewRequest()));
  }
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&virtualization_interface_status]() { return virtualization_interface_status.has_value(); },
      kTestTimeout));
  ASSERT_STATUS(virtualization_interface_status.value(), ZX_ERR_PEER_CLOSED);
  virtualization_interface_status.reset();

  // Now create the port and attach.
  constexpr uint8_t kPortID = 12;
  fidl::SynchronousInterfacePtr<fuchsia::net::tun::Port> tun_port;
  ASSERT_OK(tun_device->AddPort(
      []() {
        fuchsia::net::tun::DevicePortConfig config;
        config.set_base([]() {
          fuchsia::net::tun::BasePortConfig config;
          config.set_id(kPortID);
          config.set_rx_types({Endpoint::kFrameType});
          config.set_tx_types({{
              .type = Endpoint::kFrameType,
          }});
          return config;
        }());
        config.set_online(true);
        return config;
      }(),
      tun_port.NewRequest()));

  {
    fidl::InterfaceHandle<fuchsia::hardware::network::Port> port;
    ASSERT_OK(tun_port->GetPort(port.NewRequest()));
    ASSERT_OK(net->AddPort(std::move(port), virtualization_interface.NewRequest()));
  }

  // Wait for the session to attach to the port.
  while (true) {
    fuchsia::net::tun::InternalState state;
    ASSERT_OK(tun_port->WatchState(&state));
    if (state.has_has_session() && state.has_session()) {
      break;
    }
  }

  // Now destroy the network and observe cleanup.
  net.Unbind();
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&virtualization_interface_status]() { return virtualization_interface_status.has_value(); },
      kTestTimeout));
  ASSERT_STATUS(virtualization_interface_status.value(), ZX_ERR_PEER_CLOSED);
  virtualization_interface_status.reset();
  while (true) {
    fuchsia::net::tun::InternalState state;
    ASSERT_OK(tun_port->WatchState(&state));
    if (state.has_has_session() && !state.has_session()) {
      break;
    }
  }

  // Recreate the network and attach to it.
  CreateNetwork(netname, &net);

  {
    fidl::InterfaceHandle<fuchsia::hardware::network::Port> port;
    ASSERT_OK(tun_port->GetPort(port.NewRequest()));
    ASSERT_OK(net->AddPort(std::move(port), virtualization_interface.NewRequest()));
  }

  // Wait for the session to attach to the port.
  while (true) {
    fuchsia::net::tun::InternalState state;
    ASSERT_OK(tun_port->WatchState(&state));
    if (state.has_has_session() && state.has_session()) {
      break;
    }
  }

  // Now destroy the tun device and observe cleanup.
  tun_device.Unbind();
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&virtualization_interface_status]() { return virtualization_interface_status.has_value(); },
      kTestTimeout));
  ASSERT_STATUS(virtualization_interface_status.value(), ZX_ERR_PEER_CLOSED);
  virtualization_interface_status.reset();
}

TEST_F(NetworkServiceTest, NetworkDeviceAndVirtualization) {
  constexpr char netname[] = "mynet";
  constexpr char epname[] = "ep";
  StartServices();

  // Create a network.
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // Create and attach network device endpoint.
  fidl::SynchronousInterfacePtr<FEndpoint> ep;
  CreateEndpoint(epname, &ep, Endpoint::Backing::NETWORK_DEVICE);
  ASSERT_OK(ep->SetLinkUp(true));
  {
    zx_status_t status;
    ASSERT_OK(net->AttachEndpoint(epname, &status));
    ASSERT_OK(status);
  }

  // Start and configure ethernet client.
  fuchsia::netemul::network::DeviceConnection conn;
  ASSERT_OK(ep->GetDevice(&conn));
  ASSERT_TRUE(conn.is_network_device() && conn.network_device().is_valid());
  // Create and configure network device client.
  fidl::InterfaceHandle<fuchsia::hardware::network::Device> device;
  conn.network_device().Bind()->GetDevice(device.NewRequest());
  network::client::NetworkDeviceClient cli(fidl::HLCPPToNatural(device));
  bool ok = false;
  cli.OpenSession("test_session", [&ok](zx_status_t status) {
    ok = true;
    ASSERT_OK(status);
  });
  WAIT_FOR_OK_AND_RESET(ok);

  zx::result maybe_port_id = GetSinglePortId(cli);
  ASSERT_OK(maybe_port_id.status_value());
  const fuchsia_hardware_network::wire::PortId port_id = maybe_port_id.value();

  cli.AttachPort(port_id, {kEndpointFrameType}, [&ok](zx_status_t status) {
    ok = true;
    ASSERT_OK(status);
  });
  WAIT_FOR_OK_AND_RESET(ok);

  // Create and attach virtualized guest.
  fidl::SynchronousInterfacePtr<fuchsia::net::tun::Control> tun_ctl =
      svc_->ConnectNetworkTun().BindSync();
  std::optional<zx_status_t> tun_device_status;
  fidl::InterfacePtr<fuchsia::net::tun::Device> tun_device;
  tun_device.set_error_handler(
      [&tun_device_status](zx_status_t status) { tun_device_status = status; });
  ASSERT_OK(tun_ctl->CreateDevice(
      []() {
        fuchsia::net::tun::DeviceConfig config;
        config.set_blocking(true);
        return config;
      }(),
      tun_device.NewRequest()));
  constexpr uint8_t kPortID = 7;
  fidl::InterfacePtr<fuchsia::net::tun::Port> tun_port;
  std::optional<zx_status_t> tun_port_status;
  tun_port.set_error_handler([&tun_port_status](zx_status_t status) { tun_port_status = status; });
  tun_device->AddPort(
      []() {
        fuchsia::net::tun::DevicePortConfig config;
        config.set_base([]() {
          fuchsia::net::tun::BasePortConfig config;
          config.set_id(kPortID);
          config.set_rx_types({Endpoint::kFrameType});
          config.set_tx_types({{
              .type = Endpoint::kFrameType,
          }});
          return config;
        }());
        config.set_online(true);
        return config;
      }(),
      tun_port.NewRequest());

  fidl::InterfaceHandle<fuchsia::net::virtualization::Interface> virtualization_interface;
  {
    fidl::InterfaceHandle<fuchsia::hardware::network::Port> port;
    tun_port->GetPort(port.NewRequest());
    ASSERT_OK(net->AddPort(std::move(port), virtualization_interface.NewRequest()));
  }

  // Wait for both data paths to become ready.
  {
    bool online = false;
    bool attached = false;
    auto watcher =
        cli.WatchStatus(port_id, [&online](fuchsia_hardware_network::wire::PortStatus status) {
          if (status.flags() & fuchsia_hardware_network::wire::StatusFlags::kOnline) {
            online = true;
          }
        });
    fit::function<void(fuchsia::net::tun::InternalState)> cb;
    cb = [&cb, &tun_port, &attached](fuchsia::net::tun::InternalState state) {
      if (state.has_has_session() && state.has_session()) {
        attached = true;
      } else {
        tun_port->WatchState(
            [&cb](fuchsia::net::tun::InternalState state) { cb(std::move(state)); });
      }
    };
    tun_port->WatchState([&cb](fuchsia::net::tun::InternalState state) { cb(std::move(state)); });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&tun_device_status, &tun_port_status, &online, &attached]() {
          return tun_device_status.has_value() || tun_port_status.has_value() ||
                 (online && attached);
        },
        kTestTimeout));
    ASSERT_FALSE(tun_device_status.has_value()) << zx_status_get_string(tun_device_status.value());
    ASSERT_FALSE(tun_port_status.has_value()) << zx_status_get_string(tun_port_status.value());
  }

  // Create some test buffs.
  uint8_t test_buff1[TEST_BUF_SIZE];
  uint8_t test_buff2[TEST_BUF_SIZE];
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff1[i] = static_cast<uint8_t>(i);
    test_buff2[i] = ~static_cast<uint8_t>(i);
  }

  // Send data from virtualized guest to network device.
  {
    bool rx = false;
    cli.SetRxCallback(
        [&rx, &test_buff1, &port_id](network::client::NetworkDeviceClient::Buffer buff) {
          rx = true;
          ASSERT_EQ(buff.data().port_id().base, port_id.base);
          ASSERT_EQ(buff.data().port_id().salt, port_id.salt);
          ASSERT_EQ(buff.data().frame_type(), kEndpointFrameType);
          ASSERT_EQ(TEST_BUF_SIZE, buff.data().len());
          ASSERT_EQ(buff.data().parts(), 1u);
          ASSERT_EQ(0, memcmp(buff.data().part(0).data().data(), test_buff1, TEST_BUF_SIZE));
        });

    bool tx = false;
    tun_device->WriteFrame(
        [&test_buff1]() {
          fuchsia::net::tun::Frame frame;
          frame.set_frame_type(Endpoint::kFrameType);
          frame.set_data({std::begin(test_buff1), std::end(test_buff1)});
          frame.set_port(kPortID);
          return frame;
        }(),
        [&tx](fuchsia::net::tun::Device_WriteFrame_Result result) {
          tx = true;
          switch (result.Which()) {
            case fuchsia::net::tun::Device_WriteFrame_Result::Tag::kResponse:
              break;
            case fuchsia::net::tun::Device_WriteFrame_Result::Tag::kErr:
              FAIL() << zx_status_get_string(result.err());
              break;
            case fuchsia::net::tun::Device_WriteFrame_Result::Tag::Invalid:
              FAIL() << "invalid WriteFrame response";
          }
        });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&tun_device_status, &tun_port_status, &rx, &tx]() {
          return tun_device_status.has_value() || tun_port_status.has_value() || (rx && tx);
        },
        kTestTimeout));
    ASSERT_FALSE(tun_device_status.has_value()) << zx_status_get_string(tun_device_status.value());
    ASSERT_FALSE(tun_port_status.has_value()) << zx_status_get_string(tun_port_status.value());
  }

  // Send data from network device to virtualized guest.
  {
    auto tx = cli.AllocTx();
    ASSERT_TRUE(tx.is_valid());
    tx.data().SetPortId(port_id);
    tx.data().SetFrameType(kEndpointFrameType);
    ASSERT_EQ(tx.data().Write(test_buff2, TEST_BUF_SIZE), TEST_BUF_SIZE);
    ASSERT_OK(tx.Send());

    bool rx = false;
    tun_device->ReadFrame(
        [kPortID, &rx, &test_buff2](fuchsia::net::tun::Device_ReadFrame_Result result) {
          rx = true;
          switch (result.Which()) {
            case fuchsia::net::tun::Device_ReadFrame_Result::Tag::kResponse: {
              const fuchsia::net::tun::Frame& frame = result.response().frame;
              ASSERT_TRUE(frame.has_port());
              ASSERT_EQ(frame.port(), kPortID);
              ASSERT_TRUE(frame.has_frame_type());
              ASSERT_EQ(frame.frame_type(), Endpoint::kFrameType);
              ASSERT_TRUE(frame.has_data());
              ASSERT_EQ(TEST_BUF_SIZE, frame.data().size());
              ASSERT_EQ(0, memcmp(frame.data().data(), test_buff2, TEST_BUF_SIZE));
            } break;
            case fuchsia::net::tun::Device_ReadFrame_Result::Tag::kErr:
              FAIL() << zx_status_get_string(result.err());
              break;
            case fuchsia::net::tun::Device_ReadFrame_Result::Tag::Invalid:
              FAIL() << "invalid ReadFrame response";
          }
        });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&tun_device_status, &tun_port_status, &rx]() {
          return tun_device_status.has_value() || tun_port_status.has_value() || rx;
        },
        kTestTimeout));
    ASSERT_FALSE(tun_device_status.has_value()) << zx_status_get_string(tun_device_status.value());
    ASSERT_FALSE(tun_port_status.has_value()) << zx_status_get_string(tun_port_status.value());
  }

  // Drop the virtualized guest.
  virtualization_interface.TakeChannel().reset();
  // Check that the session was destroyed.
  {
    bool detached = false;
    fit::function<void(fuchsia::net::tun::InternalState)> cb;
    cb = [&cb, &tun_port, &detached](fuchsia::net::tun::InternalState state) {
      if (state.has_has_session() && !state.has_session()) {
        detached = true;
      } else {
        tun_port->WatchState(
            [&cb](fuchsia::net::tun::InternalState state) { cb(std::move(state)); });
      }
    };
    tun_port->WatchState([&cb](fuchsia::net::tun::InternalState state) { cb(std::move(state)); });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&tun_device_status, &tun_port_status, &detached]() {
          return tun_device_status.has_value() || tun_port_status.has_value() || detached;
        },
        kTestTimeout));
    ASSERT_FALSE(tun_device_status.has_value()) << zx_status_get_string(tun_device_status.value());
    ASSERT_FALSE(tun_port_status.has_value()) << zx_status_get_string(tun_port_status.value());
  }
}

}  // namespace testing
}  // namespace netemul
