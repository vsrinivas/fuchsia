// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "roughtime_server.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <thread>

#include "gtest/gtest.h"
#include "lib/fxl/files/unique_fd.h"

#define PORT 3453

namespace timeservice {

TEST(RoughTimeServerTest, TestValid) {
  uint8_t key[ED25519_PUBLIC_KEY_LEN] = {0};
  RoughTimeServer server1("name", "address:3424", key,
                          ED25519_PUBLIC_KEY_LEN + 1);
  EXPECT_EQ(server1.IsValid(), false);

  RoughTimeServer server2("name", "address:3424", key, ED25519_PUBLIC_KEY_LEN);
  EXPECT_EQ(server2.IsValid(), true);
}

#define BUFSIZE 1024
void listen(int sock) {
  struct sockaddr_in client;
  int attempts = 3;
  int ret;
  do {
    attempts--;
    int timeout = 3 * 1000;
    pollfd readfd;
    readfd.fd = sock;
    readfd.events = POLLIN;
    ret = poll(&readfd, 1, timeout);
  } while (attempts > 0 && ret != 1);
  ASSERT_NE(ret, 0) << "poll timeout";
  ASSERT_EQ(ret, 1) << "poll: " << strerror(errno);
  char buf[BUFSIZE];
  socklen_t len = sizeof(client);
  int n = recvfrom(sock, buf, BUFSIZE, 0, (struct sockaddr*)&client, &len);
  ASSERT_GE(n, 0) << "recvfrom: " << strerror(errno);
  struct hostent* host = gethostbyaddr((const char*)&client.sin_addr.s_addr,
                                       sizeof(client.sin_addr.s_addr), AF_INET);
  ASSERT_NE(host, nullptr) << "gethostbyaddr: " << strerror(errno);
  char* hostaddr = inet_ntoa(client.sin_addr);
  ASSERT_NE(hostaddr, nullptr) << "inet_ntoa: " << strerror(errno);
  n = sendto(sock, buf, strlen(buf), 0, (struct sockaddr*)&client, len);
  ASSERT_GE(n, 0) << "sendto: " << strerror(errno);
}

// Checks that server recieves request from time-service
TEST(RoughTimeServerTest, TestServerRequest) {
  uint8_t key[ED25519_PUBLIC_KEY_LEN] = {0};
  RoughTimeServer server("name", "127.0.0.1:" + std::to_string(PORT), key,
                         ED25519_PUBLIC_KEY_LEN);
  EXPECT_EQ(server.IsValid(), true);

  // Start server
  struct sockaddr_in serveraddr;

  memset(&serveraddr, 0, sizeof(serveraddr));
  fxl::UniqueFD sock_ufd(socket(AF_INET, SOCK_DGRAM, 0));
  ASSERT_TRUE(sock_ufd.is_valid())
      << "udp server: socket call" << strerror(errno);
  int sock = sock_ufd.get();

  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = INADDR_ANY;
  serveraddr.sin_port = htons(PORT);

  ASSERT_EQ(bind(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr)), 0)
      << "binding udp: " << strerror(errno);
  std::thread t1(listen, sock);

  roughtime::rough_time_t t;
  timeservice::Status ret;
  int attempts = 3;
  do {
    attempts--;
    ret = server.GetTimeFromServer(&t);
  } while (attempts > 0 && ret == timeservice::Status::NETWORK_ERROR);
  t1.join();
}

}  // namespace timeservice
