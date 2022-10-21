/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "adb-protocol.h"
#define TRACE_TAG TRANSPORT

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "adb-base.h"
#include "transport.h"

#if 0
#include "adb.h"
#include "adb_auth.h"
#include "adb_io.h"
#include "adb_trace.h"
#include "adb_utils.h"
#include "fdevent/fdevent.h"
#include "sysdeps.h"
#include "sysdeps/chrono.h"
#endif

using namespace std::string_literals;

// static void remove_transport(atransport* transport);
// static void transport_destroy(atransport* transport);

// TODO: unordered_map<TransportId, atransport*>
static auto& transport_list = *new std::list<atransport*>();
static auto& pending_list = *new std::list<atransport*>();

static auto& transport_lock = *new std::recursive_mutex();

const char* const kFeatureShell2 = "shell_v2";
const char* const kFeatureCmd = "cmd";
const char* const kFeatureStat2 = "stat_v2";
const char* const kFeatureLs2 = "ls_v2";
const char* const kFeatureLibusb = "libusb";
const char* const kFeaturePushSync = "push_sync";
const char* const kFeatureApex = "apex";
const char* const kFeatureFixedPushMkdir = "fixed_push_mkdir";
const char* const kFeatureAbb = "abb";
const char* const kFeatureFixedPushSymlinkTimestamp = "fixed_push_symlink_timestamp";
const char* const kFeatureAbbExec = "abb_exec";
const char* const kFeatureRemountShell = "remount_shell";
const char* const kFeatureTrackApp = "track_app";
const char* const kFeatureSendRecv2 = "sendrecv_v2";
const char* const kFeatureSendRecv2Brotli = "sendrecv_v2_brotli";
const char* const kFeatureSendRecv2LZ4 = "sendrecv_v2_lz4";
const char* const kFeatureSendRecv2Zstd = "sendrecv_v2_zstd";
const char* const kFeatureSendRecv2DryRunSend = "sendrecv_v2_dry_run_send";
const char* const kFeatureDelayedAck = "delayed_ack";
// TODO(joshuaduong): Bump to v2 when openscreen discovery is enabled by default
const char* const kFeatureOpenscreenMdns = "openscreen_mdns";

TransportId NextTransportId() {
  static std::atomic<TransportId> next(1);
  return next++;
}

void Connection::Reset() {
  FX_LOGS(DEBUG) << "Connection::Reset(): stopping";
  Stop();
}

std::string Connection::Serial() const {
  return transport_ ? transport_->serial_name() : "<unknown>";
}

BlockingConnectionAdapter::BlockingConnectionAdapter(std::unique_ptr<BlockingConnection> connection)
    : underlying_(std::move(connection)) {}

BlockingConnectionAdapter::~BlockingConnectionAdapter() {
  FX_LOGS(DEBUG) << "BlockingConnectionAdapter (" << Serial().c_str() << ")): destructing";
  Stop();
}

void BlockingConnectionAdapter::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_) {
    FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str()
                   << "): started multiple times";
  }

  // StartReadThread();

  write_thread_ = std::thread([this]() {
    FX_LOGS(DEBUG) << Serial().c_str() << "write thread spawning";
    while (true) {
      std::unique_lock<std::mutex> lock(mutex_);
      // ScopedLockAssertion assume_locked(mutex_);
      cv_.wait(lock, [this]() __TA_REQUIRES(mutex_) {
        return this->stopped_ || !this->write_queue_.empty();
      });

      if (this->stopped_) {
        return;
      }

      std::unique_ptr<apacket> packet = std::move(this->write_queue_.front());
      this->write_queue_.pop_front();
      lock.unlock();

      if (!this->underlying_->Write(packet.release())) {
        break;
      }
    }
    std::call_once(this->error_flag_, [this]() { transport_->HandleError("write failed"); });
  });

  started_ = true;
}

void BlockingConnectionAdapter::StartReadThread() {
  read_thread_ = std::thread([this]() {
    FX_LOGS(DEBUG) << Serial().c_str() << " read thread spawning";
    while (true) {
      auto packet = std::make_unique<apacket>();
      if (!underlying_->Read(packet.get())) {
        FX_LOGS(ERROR) << Serial().c_str() << "read failed";
        break;
      }

      bool got_stls_cmd = false;
      if (packet->msg.command == A_STLS) {
        got_stls_cmd = true;
      }

      transport_->HandleRead(std::move(packet));

      // If we received the STLS packet, we are about to perform the TLS
      // handshake. So this read thread must stop and resume after the
      // handshake completes otherwise this will interfere in the process.
      if (got_stls_cmd) {
        FX_LOGS(DEBUG) << Serial().c_str() << "Received STLS packet. Stopping read thread.";
        return;
      }
    }
    std::call_once(this->error_flag_, [this]() { transport_->HandleError("read failed"); });
  });
}

bool BlockingConnectionAdapter::DoTlsHandshake(/*RSA*/ char* key, std::string* auth_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (read_thread_.joinable()) {
    read_thread_.join();
  }
  bool success = this->underlying_->DoTlsHandshake(key, auth_key);
  StartReadThread();
  return success;
}

void BlockingConnectionAdapter::Reset() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) {
      FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str() << "): not started";
      return;
    }

    if (stopped_) {
      FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str() << "): already stopped";
      return;
    }
  }

  FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str() << "): resetting";
  this->underlying_->Reset();
  Stop();
}

void BlockingConnectionAdapter::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) {
      FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str() << "): not started";
      return;
    }

    if (stopped_) {
      FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str() << "): already stopped";
      return;
    }

    stopped_ = true;
  }

  FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str() << "): stopping";

  this->underlying_->Close();
  this->cv_.notify_one();

  // Move the threads out into locals with the lock taken, and then unlock to let them exit.
  std::thread read_thread;
  std::thread write_thread;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    read_thread = std::move(read_thread_);
    write_thread = std::move(write_thread_);
  }

  if (read_thread.joinable()) {
    read_thread.join();
  }
  if (write_thread.joinable()) {
    write_thread.join();
  }

  FX_LOGS(DEBUG) << "BlockingConnectionAdapter(" << Serial().c_str() << "): stopped";
  // std::call_once(this->error_flag_, [this]() { transport_->HandleError("requested stop"); });
}

bool BlockingConnectionAdapter::Write(std::unique_ptr<apacket> packet) {
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    write_queue_.emplace_back(std::move(packet));
  }

  cv_.notify_one();
  return true;
}

void* BlockingConnectionAdapter::adb() { return underlying_->adb(); }

FdConnection::FdConnection(void* adb) : adb_(adb) {}

FdConnection::~FdConnection() {}

bool FdConnection::DispatchRead(void* buf, size_t len) {
  FX_LOGS(ERROR) << "This part is not implemented";
#if 0
  if (tls_ != nullptr) {
    // The TlsConnection doesn't allow 0 byte reads
    if (len == 0) {
      return true;
    }
    return false;/*tls_->ReadFully(buf, len);*/
  }
#endif

  return false; /*ReadFdExactly(fd_.get(), buf, len);*/
}

bool FdConnection::DispatchWrite(void* buf, size_t len) {
#if 0
  if (tls_ != nullptr) {
    // The TlsConnection doesn't allow 0 byte writes
    if (len == 0) {
      return true;
    }
    return false;/*tls_->WriteFully(std::string_view(reinterpret_cast<const char*>(buf), len));*/
  }
#endif

  if (!adb_) {
    return false;
  }
  auto* adb = static_cast<adb::AdbBase*>(adb_);
  return adb->SendUsbPacket(reinterpret_cast<uint8_t*>(buf), len);
}

bool FdConnection::Read(apacket* packet) {
  if (!DispatchRead(&packet->msg, sizeof(amessage))) {
    FX_LOGS(DEBUG) << "remote local: read terminated (message)";
    return false;
  }

  if (packet->msg.data_length > MAX_PAYLOAD) {
    // zxlogf(INFO, "remote local: read overflow (data length = %" PRIu32 ")",
    //        packet->msg.data_length);
    return false;
  }

  packet->payload.resize(packet->msg.data_length);

  if (!DispatchRead(&packet->payload[0], packet->payload.size())) {
    FX_LOGS(DEBUG) << "remote local: terminated (data)";
    return false;
  }

  return true;
}

bool FdConnection::Write(apacket* packet) {
  if (!DispatchWrite(&packet->msg, sizeof(packet->msg))) {
    FX_LOGS(DEBUG) << "remote local: write terminated";
    put_apacket(packet);
    return false;
  }

  if (packet->msg.data_length) {
    if (!DispatchWrite(&packet->payload[0], packet->msg.data_length)) {
      FX_LOGS(DEBUG) << "remote local: write terminated";
      put_apacket(packet);
      return false;
    }
  }

  put_apacket(packet);
  return true;
}

bool FdConnection::DoTlsHandshake(/*RSA*/ char* key, std::string* auth_key) {
#if 0
  bssl::UniquePtr<EVP_PKEY> evp_pkey(EVP_PKEY_new());
  if (!EVP_PKEY_set1_RSA(evp_pkey.get(), key)) {
    LOG(ERROR) << "EVP_PKEY_set1_RSA failed";
    return false;
  }
  auto x509 = GenerateX509Certificate(evp_pkey.get());
  auto x509_str = X509ToPEMString(x509.get());
  auto evp_str = Key::ToPEMString(evp_pkey.get());

  int osh = cast_handle_to_int(adb_get_os_handle(fd_));

  tls_ = TlsConnection::Create(TlsConnection::Role::Server, x509_str, evp_str, osh);

  CHECK(tls_);
  // Add callback to check certificate against a list of known public keys
  tls_->SetCertVerifyCallback(
      [auth_key](X509_STORE_CTX* ctx) { return adbd_tls_verify_cert(ctx, auth_key); });
  // Add the list of allowed client CA issuers
  auto ca_list = adbd_tls_client_ca_list();
  tls_->SetClientCAList(ca_list.get());

  auto err = tls_->DoHandshake();
  if (err == TlsError::Success) {
    return true;
  }

  tls_.reset();
#endif
  return false;
}

void FdConnection::Close() {
  adb_ = nullptr;
  /*
  adb_shutdown(fd_.get());
  fd_.reset();
  */
}

std::string dump_hex(const void* data, size_t byte_count) {
  size_t truncate_len = 16;
  bool truncated = false;
  if (byte_count > truncate_len) {
    byte_count = truncate_len;
    truncated = true;
  }

  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);

  std::string line;
  for (size_t i = 0; i < byte_count; ++i) {
    if ((i % 4) == 0) {
      line += " 0x";
    }
    char byte[32];
    sprintf(byte, "%02x", p[i]);
    line += byte;
  }
  line.push_back(' ');

  for (size_t i = 0; i < byte_count; ++i) {
    uint8_t ch = p[i];
    line.push_back(isprint(ch) ? ch : '.');
  }

  if (truncated) {
    line += " [truncated]";
  }

  return line;
}

std::string dump_header(const amessage* msg) {
  unsigned command = msg->command;
  int len = msg->data_length;
  char cmd[9];
  char arg0[12], arg1[12];
  int n;

  for (n = 0; n < 4; n++) {
    int b = (command >> (n * 8)) & 255;
    if (b < 32 || b >= 127)
      break;
    cmd[n] = (char)b;
  }
  if (n == 4) {
    cmd[4] = 0;
  } else {
    // There is some non-ASCII name in the command, so dump the hexadecimal value instead
    snprintf(cmd, sizeof cmd, "%08x", command);
  }

  if (msg->arg0 < 256U)
    snprintf(arg0, sizeof arg0, "%d", msg->arg0);
  else
    snprintf(arg0, sizeof arg0, "0x%x", msg->arg0);

  if (msg->arg1 < 256U)
    snprintf(arg1, sizeof arg1, "%d", msg->arg1);
  else
    snprintf(arg1, sizeof arg1, "0x%x", msg->arg1);

  char output[1024];
  sprintf(output, "[%s] arg0=%s arg1=%s (len=%d) ", cmd, arg0, arg1, len);
  return output;
}

std::string dump_packet(const char* name, const char* func, const apacket* p) {
  std::string result = name;
  result += ": ";
  result += func;
  result += ": ";
  result += dump_header(&p->msg);
  result += dump_hex(p->payload.data(), p->payload.size());
  return result;
}

void send_packet(apacket* p, atransport* t) {
  p->msg.magic = p->msg.command ^ 0xffffffff;
  // compute a checksum for connection/auth packets for compatibility reasons
  if (t->get_protocol_version() >= A_VERSION_SKIP_CHECKSUM) {
    p->msg.data_check = 0;
  } else {
    p->msg.data_check = calculate_apacket_checksum(p);
  }

  if (p->payload.size()) {
    FX_LOGS(DEBUG) << dump_packet(t->serial.c_str(), "to remote", p).c_str();
  }

  if (t == nullptr) {
    FX_LOGS(ERROR) << "Transport is null";
  }

  if (t->Write(p) != 0) {
    FX_LOGS(ERROR) << t->serial.c_str() << "failed to enqueue packet, closing transport";
    t->Kick();
  }
}

void kick_transport(atransport* t, bool reset) {
  std::lock_guard<std::recursive_mutex> lock(transport_lock);
  // As kick_transport() can be called from threads without guarantee that t is valid,
  // check if the transport is in transport_list first.
  //
  // TODO(jmgao): WTF? Is this actually true?
  if (std::find(transport_list.begin(), transport_list.end(), t) != transport_list.end()) {
    if (reset) {
      t->Reset();
    } else {
      t->Kick();
    }
  }
}

static int transport_registration_send = -1;
// static int transport_registration_recv = -1;
// static fdevent* transport_registration_fde;

void update_transports() {
  // Nothing to do on the device side.
}

struct tmsg {
  atransport* transport;
  int action;
};

/* Used to retry syscalls that can return EINTR. */
#define TEMP_FAILURE_RETRY(exp)            \
  ({                                       \
    __typeof__(exp) _rc;                   \
    do {                                   \
      _rc = (exp);                         \
    } while (_rc == -1 && errno == EINTR); \
    _rc;                                   \
  })

#if 0
static inline int adb_read(int fd, void* buf, size_t len) {
    return static_cast<int>(TEMP_FAILURE_RETRY(read(fd, buf, len)));
}
#endif

static inline int adb_write(int fd, const void* buf, size_t len) {
  return static_cast<int>(TEMP_FAILURE_RETRY(write(fd, buf, len)));
}

#if 0
static int transport_read_action(int fd, struct tmsg* m) {
  char* p = (char*)m;
  int len = sizeof(*m);
  int r;

  while (len > 0) {
    r = adb_read(fd, p, len);
    if (r > 0) {
      len -= r;
      p += r;
    } else {
      zxlogf(INFO,"transport_read_action: on fd %d: %s", fd, strerror(errno));
      return -1;
    }
  }
  return 0;
}
#endif

static int transport_write_action(int fd, struct tmsg* m) {
  char* p = (char*)m;
  int len = sizeof(*m);
  int r;

  while (len > 0) {
    r = adb_write(fd, p, len);
    if (r > 0) {
      len -= r;
      p += r;
    } else {
      FX_LOGS(ERROR) << "transport_write_action: on fd " << fd << ": " << strerror(errno);
      return -1;
    }
  }
  return 0;
}

#if 0
static bool usb_devices_start_detached() { return false; }
#endif

#if 0
static void transport_registration_func(int _fd, unsigned ev, void*) {
  tmsg m;
  atransport* t;

  if (!(ev & 0x0001/* FDE_READ*/)) {
    return;
  }

  if (transport_read_action(_fd, &m)) {
    zxlogf(ERROR,"cannot read transport registration socket");
  }

  t = m.transport;

  if (m.action == 0) {
    zxlogf(INFO,"transport: %s deleting", t->serial.c_str());

    {
      std::lock_guard<std::recursive_mutex> lock(transport_lock);
      transport_list.remove(t);
    }

    delete t;

    update_transports();
    return;
  }

  /* don't create transport threads for inaccessible devices */
  if (t->GetConnectionState() != kCsNoPerm) {
    t->connection()->SetTransport(t);

    if (t->type == kTransportUsb && usb_devices_start_detached()) {
      t->SetConnectionState(kCsDetached);
    } else {
      t->connection()->Start();
    }
  }

  {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    auto it = std::find(pending_list.begin(), pending_list.end(), t);
    if (it != pending_list.end()) {
      pending_list.remove(t);
      transport_list.push_front(t);
    }
  }

  update_transports();
}
#endif

void init_transport_registration(void) {
#if 0
  int s[2];
  if (adb_socketpair(s)) {
    zxlogf(ERROR,"cannot open transport registration socketpair");
  }
  zxlogf(INFO,"socketpair: (%d,%d)", s[0], s[1]);

  transport_registration_send = s[0];
  transport_registration_recv = s[1];
  transport_registration_fde =
      fdevent_create(transport_registration_recv, transport_registration_func, nullptr);
  fdevent_set(transport_registration_fde, FDE_READ);
#endif
}

void kick_all_transports() {
  // To avoid only writing part of a packet to a transport after exit, kick all transports.
  std::lock_guard<std::recursive_mutex> lock(transport_lock);
  for (auto t : transport_list) {
    t->Kick();
  }
}

void kick_all_tcp_tls_transports() {
  std::lock_guard<std::recursive_mutex> lock(transport_lock);
  for (auto t : transport_list) {
    if (t->IsTcpDevice() && t->use_tls) {
      t->Kick();
    }
  }
}

void kick_all_transports_by_auth_key(std::string_view auth_key) {
  std::lock_guard<std::recursive_mutex> lock(transport_lock);
  for (auto t : transport_list) {
    if (auth_key == t->auth_key) {
      t->Kick();
    }
  }
}

/* the fdevent select pump is single threaded */
void register_transport(atransport* transport) {
  tmsg m;
  m.transport = transport;
  m.action = 1;
  FX_LOGS(DEBUG) << "transport: " << transport->serial.c_str() << " registered";
  if (transport_write_action(transport_registration_send, &m)) {
    FX_LOGS(ERROR) << "cannot write transport registration socket";
  }
}

#if 0
static void remove_transport(atransport* transport) {
  tmsg m;
  m.transport = transport;
  m.action = 0;
  zxlogf(INFO,"transport: %s removed", transport->serial.c_str());
  if (transport_write_action(transport_registration_send, &m)) {
    zxlogf(ERROR,"cannot write transport registration socket");
  }
}
#endif

#if 0
static void transport_destroy(atransport* t) {
  //check_main_thread();
  ZX_ASSERT(t != nullptr);

  std::lock_guard<std::recursive_mutex> lock(transport_lock);
  zxlogf(INFO,"destroying transport %s", t->serial_name().c_str());
  t->connection()->Stop();
   zxlogf(INFO,"transport: %s destroy (kicking and closing)", t->serial.c_str());
  remove_transport(t);
}
#endif

atransport::~atransport() {}

int atransport::Write(apacket* p) {
  return this->connection()->Write(std::unique_ptr<apacket>(p)) ? 0 : -1;
}

void atransport::Reset() {
  if (!kicked_.exchange(true)) {
    FX_LOGS(DEBUG) << "resetting transport" << this << this->serial.c_str();
    this->connection()->Reset();
  }
}

void atransport::Kick() {
  if (!kicked_.exchange(true)) {
    FX_LOGS(DEBUG) << "kicking transport" << this << this->serial.c_str();
    this->connection()->Stop();
  }
}

ConnectionState atransport::GetConnectionState() const { return connection_state_; }

void atransport::SetConnectionState(ConnectionState state) {
  // check_main_thread();
  connection_state_ = state;
  update_transports();
}

void atransport::SetConnection(std::shared_ptr<Connection> connection) {
  std::lock_guard<std::mutex> lock(mutex_);
  connection_ = std::shared_ptr<Connection>(std::move(connection));
}

bool atransport::HandleRead(std::unique_ptr<apacket> p) {
  if (!check_header(p.get(), this)) {
    FX_LOGS(DEBUG) << serial.c_str() << ": remote read: bad header";
    return false;
  }

  // zxlogf(INFO, "%s", dump_packet(serial.c_str(), "from remote", p.get()).c_str());

  // TODO: Does this need to run on the main thread?
  apacket* packet = p.release();
  handle_packet(packet, this);
  return true;
}

void atransport::HandleError(const std::string& error) {
  // FX_LOGS(DEBUG) << serial_name().c_str() << ": connection terminated: " << error.c_str();
#if 0
  fdevent_run_on_main_thread([this]() {
    handle_offline(this);
    transport_destroy(this);
  });
#endif
}

void atransport::update_version(int version, size_t payload) {
  protocol_version = std::min(version, A_VERSION);
  max_payload = std::min(payload, MAX_PAYLOAD);
}

int atransport::get_protocol_version() const { return protocol_version; }

int atransport::get_tls_version() const { return tls_version; }

size_t atransport::get_max_payload() const { return max_payload; }

const FeatureSet& supported_features() {
  static const FeatureSet features([]() {
    // Increment ADB_SERVER_VERSION when adding a feature that adbd needs
    // to know about. Otherwise, the client can be stuck running an old
    // version of the server even after upgrading their copy of adb.
    // (http://b/24370690)

    // clang-format off
        FeatureSet result {
            kFeatureShell2,
            kFeatureCmd,
            kFeatureStat2,
            kFeatureLs2,
            kFeatureFixedPushMkdir,
            kFeatureApex,
            kFeatureAbb,
            kFeatureFixedPushSymlinkTimestamp,
            kFeatureAbbExec,
            kFeatureRemountShell,
            kFeatureTrackApp,
            kFeatureSendRecv2,
            kFeatureSendRecv2Brotli,
            kFeatureSendRecv2LZ4,
            kFeatureSendRecv2Zstd,
            kFeatureSendRecv2DryRunSend,
            kFeatureOpenscreenMdns,
        };
    // clang-format on

    result.push_back(kFeatureDelayedAck);

    return result;
  }());

  return features;
}

std::string FeatureSetToString(const FeatureSet& features) {
  std::string output = "";
  for (auto f : features) {
    output += f + ',';
  }
  return output;
}

FeatureSet StringToFeatureSet(const std::string& features_string) {
  if (features_string.empty()) {
    return FeatureSet();
  }

  return Split(features_string, ",");
}

template <class Range, class Value>
static bool contains(const Range& r, const Value& v) {
  return std::find(std::begin(r), std::end(r), v) != std::end(r);
}

bool CanUseFeature(const FeatureSet& feature_set, const std::string& feature) {
  return contains(feature_set, feature) && contains(supported_features(), feature);
}

bool atransport::has_feature(const std::string& feature) const {
  return contains(features_, feature);
}

void atransport::SetFeatures(const std::string& features_string) {
  features_ = StringToFeatureSet(features_string);
  delayed_ack_ = CanUseFeature(features_, kFeatureDelayedAck);
}

void atransport::AddDisconnect(adisconnect* disconnect) { disconnects_.push_back(disconnect); }

void atransport::RemoveDisconnect(adisconnect* disconnect) { disconnects_.remove(disconnect); }

void atransport::RunDisconnects() {
  for (const auto& disconnect : disconnects_) {
    disconnect->func(disconnect->opaque, this);
  }
  disconnects_.clear();
}

#if 0
bool register_socket_transport(unique_fd s, std::string serial, int port, int local,
                               atransport::ReconnectCallback reconnect, bool use_tls, int* error) {
  atransport* t = new atransport(std::move(reconnect), kCsOffline);
  t->use_tls = use_tls;

  zxlogf(INFO,"transport: %s init'ing for socket %d, on port %d", serial.c_str(), s.get(), port);
  if (init_socket_transport(t, std::move(s), port, local) < 0) {
    delete t;
    if (error)
      *error = errno;
    return false;
  }

  std::unique_lock<std::recursive_mutex> lock(transport_lock);
  for (const auto& transport : pending_list) {
    if (serial == transport->serial) {
      zxlogf(INFO, "socket transport %s is already in pending_list and fails to register", transport->serial);
      delete t;
      if (error)
        *error = EALREADY;
      return false;
    }
  }

  for (const auto& transport : transport_list) {
    if (serial == transport->serial) {
      zxlogf(INFO, "socket transport %s is already in transport_list and fails to register", transport->serial);
      delete t;
      if (error)
        *error = EALREADY;
      return false;
    }
  }

  t->serial = std::move(serial);
  pending_list.push_front(t);

  lock.unlock();

  register_transport(t);

  if (local == 1) {
    // Do not wait for emulator transports.
    return true;
  }

  return true;
}
#endif

bool check_header(apacket* p, atransport* t) {
  if (p->msg.magic != (p->msg.command ^ 0xffffffff)) {
    FX_LOGS(ERROR) << "check_header(): invalid magic command = " << p->msg.command
                   << ", magic = " << p->msg.magic;
    return false;
  }

  if (p->msg.data_length > t->get_max_payload()) {
    FX_LOGS(ERROR) << "check_header(): " << p->msg.data_length
                   << " atransport::max_payload = " << t->get_max_payload();
    return false;
  }

  return true;
}
