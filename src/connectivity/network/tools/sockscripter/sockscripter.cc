// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sockscripter.h"

#include <getopt.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <climits>
#include <iostream>

#include "addr.h"
#include "log.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "util.h"

#define PRINT_SOCK_OPT_VAL(level, name) \
  LOG(INFO) << #level "=" << (level) << " " << #name << "=" << name

#define SET_SOCK_OPT_VAL(level, name, val)                                     \
  {                                                                            \
    if (api_->setsockopt(sockfd_, (level), (name), &(val), sizeof(val)) < 0) { \
      LOG(ERROR) << "Failed to set socket option " #level ":" #name "-"        \
                 << "[" << errno << "]" << strerror(errno);                    \
      return false;                                                            \
    }                                                                          \
    LOG(INFO) << "Set " #level ":" #name " = " << (int)(val);                  \
    return true;                                                               \
  }

#define LOG_SOCK_OPT_VAL(level, name, type_val)                             \
  {                                                                         \
    type_val opt = 0;                                                       \
    socklen_t opt_len = sizeof(opt);                                        \
    if (api_->getsockopt(sockfd_, (level), (name), &(opt), &opt_len) < 0) { \
      LOG(ERROR) << "Error-Getting " #level ":" #name "-"                   \
                 << "[" << errno << "]" << strerror(errno);                 \
      return false;                                                         \
    }                                                                       \
    LOG(INFO) << #level ":" #name " is set to " << (int)opt;                \
    return true;                                                            \
  }

bool TestRepeatCfg::Parse(const std::string& cmd) {
  command = "";
  repeat_count = 1;
  delay_ms = 0;
  if (cmd[0] != '{') {
    return false;
  }

  auto cmd_e = cmd.find_first_of('}');
  if (cmd_e == std::string::npos) {
    return false;
  }
  command = cmd.substr(1, cmd_e - 1);

  auto n_t_str = cmd.substr(cmd_e + 1);
  if (n_t_str.empty()) {
    return true;
  }

  auto n_num_str_i = n_t_str.find("N=");
  if (n_num_str_i != std::string::npos) {
    auto n_num_str = n_t_str.substr(n_num_str_i + 2);
    if (!str2int(n_num_str, &repeat_count)) {
      LOG(ERROR) << "Error parsing '" << cmd << "': in N=<n>, <n> is not a number!";
      return false;
    }
  }
  auto t_num_str_i = n_t_str.find("T=");
  if (t_num_str_i != std::string::npos) {
    auto t_num_str = n_t_str.substr(t_num_str_i + 2);
    if (!str2int(t_num_str, &delay_ms)) {
      LOG(ERROR) << "Error parsing '" << cmd << "': in T=<t>, <t> is not a number!";
    }
  }
  return true;
}

struct SocketType {
  const char* name;
  const char* arg;
  const char* help_str;
  int domain;
  int type;
} socket_types[] = {
    {"udp", nullptr, "open new AF_INET SOCK_DGRAM socket", AF_INET, SOCK_DGRAM},
    {"udp6", nullptr, "open new AF_INET6 SOCK_DGRAM socket", AF_INET6, SOCK_DGRAM},
    {"tcp", nullptr, "open new AF_INET SOCK_STREAM socket", AF_INET, SOCK_STREAM},
    {"tcp6", nullptr, "open new AF_INET6 SOCK_STREAM socket", AF_INET6, SOCK_STREAM},
    {"raw", "<protocol>", "open new AF_INET SOCK_RAW socket", AF_INET, SOCK_RAW},
    {"raw6", "<protocol>", "open new AF_INET6 SOCK_RAW socket", AF_INET6, SOCK_RAW},
};

typedef bool (SockScripter::*SockScripterHandler)(char* arg);
const struct Command {
  // Command name as used on the commandline.
  const char* name;
  // Argument description, nullptr if the command doesn't have an argument.
  const char* opt_arg_descr;
  // String displayed in the help text.
  const char* help_str;
  SockScripterHandler handler;
} kCommands[] = {
    {"close", nullptr, "close socket", &SockScripter::Close},
    {"bind", "<bind-ip>:<bind-port>", "bind to the given local address", &SockScripter::Bind},
    {"bound", nullptr, "log bound-to-address", &SockScripter::LogBoundToAddress},
    {"log-peername", nullptr, "log peer name", &SockScripter::LogPeerAddress},
    {"connect", "<connect-ip>:<connect-port>", "connect to the given remote address",
     &SockScripter::Connect},
    {"disconnect", nullptr, "disconnect a connected socket", &SockScripter::Disconnect},
    {"set-broadcast", "{0|1}", "set SO_BROADCAST flag", &SockScripter::SetBroadcast},
    {"log-broadcast", nullptr, "log SO_BROADCAST option flag", &SockScripter::LogBroadcast},
    {"set-bindtodevice", "<if-name-string>", "set SO_BINDTODEVICE", &SockScripter::SetBindToDevice},
    {"log-bindtodevice", nullptr, "log SO_BINDTODEVICE option value",
     &SockScripter::LogBindToDevice},
    {"set-reuseaddr", "{0|1}", "set SO_REUSEADDR flag", &SockScripter::SetReuseaddr},
    {"log-reuseaddr", nullptr, "log SO_REUSEADDR option value", &SockScripter::LogReuseaddr},
    {"set-reuseport", "{0|1}", "set SO_REUSEPORT flag", &SockScripter::SetReuseport},
    {"log-reuseport", nullptr, "log SO_REUSEPORT option value", &SockScripter::LogReuseport},
    {"set-unicast-ttl", "<ttl-for-IP_TTL>", "set TTL for V4 unicast packets",
     &SockScripter::SetIpUnicastTTL},
    {"log-unicast-ttl", nullptr, "log IP_TTL option value", &SockScripter::LogIpUnicastTTL},
    {"set-unicast-hops", "<ttl-for-IPV6_UNICAST_HOPS>", "set hops for V6 unicast packets",
     &SockScripter::SetIpUnicastHops},
    {"log-unicast-hops", nullptr, "log IPV6_UNICAST_HOPS option value",
     &SockScripter::LogIpUnicastHops},
    {"set-mcast-ttl", "<ttl-for-IP_MULTICAST-TTL>", "set TTL for V4 mcast packets",
     &SockScripter::SetIpMcastTTL},
    {"log-mcast-ttl", nullptr, "log IP_MULTICAST_TTL option value", &SockScripter::LogIpMcastTTL},
    {"set-mcast-loop4", "{1|0}", "set IP_MULTICAST_LOOP flag", &SockScripter::SetIpMcastLoop4},
    {"log-mcast-loop4", nullptr, "log IP_MULTICAST_LOOP option value",
     &SockScripter::LogIpMcastLoop4},
    {"set-mcast-hops", "<ttl-for-IPV6_MULTICAST_HOPS>", "set hops for V6 mcast packets",
     &SockScripter::SetIpMcastHops},
    {"log-mcast-hops", nullptr, "log IPV6_MULTICAST_HOPS option value",
     &SockScripter::LogIpMcastHops},
    {"set-mcast-loop6", "{1|0}", "set IPV6_MULTICAST_LOOP flag", &SockScripter::SetIpMcastLoop6},
    {"log-mcast-loop6", nullptr, "log IPV6_MULTICAST_LOOP option value",
     &SockScripter::LogIpMcastLoop6},
    {"set-mcast-if4", "{<local-intf-Addr>}", "set IP_MULTICAST_IF for IPv4 mcast",
     &SockScripter::SetIpMcastIf4},
    {"log-mcast-if4", nullptr, "log IP_MULTICAST_IF option value", &SockScripter::LogIpMcastIf4},
    {"set-mcast-if6", "<local-intf-id>", "set IP_MULTICAST_IF6 for IPv6 mcast",
     &SockScripter::SetIpMcastIf6},
    {"log-mcast-if6", nullptr, "log IP_MULTICAST_IF6 option value", &SockScripter::LogIpMcastIf6},

    {"set-ipv6-only", "{0|1}", "set IPV6_V6ONLY flag", &SockScripter::SetIpV6Only},
    {"log-ipv6-only", nullptr, "log IPV6_V6ONLY option value", &SockScripter::LogIpV6Only},

    {"join4", "<mcast-ip>-<local-intf-Addr>",
     "join IPv4 mcast group (IP_ADD_MEMBERSHIP) on local interface", &SockScripter::Join4},
    {"drop4", "<mcast-ip>-<local-intf-Addr>",
     "drop IPv4 mcast group (IP_DROP_MEMBERSHIP) on local interface", &SockScripter::Drop4},
    {"join6", "<mcast-ip>-<local-intf-id>",
     "join IPv6 mcast group (IPV6_ADD_MEMBERSHIP/IPV6_JOIN_GROUP) on local interface",
     &SockScripter::Join6},
    {"drop6", "<mcast-ip>-<local-intf-id>",
     "drop IPv6 mcast group (IPV6_DROP_MEMBERSHIP/IPV6_LEAVE_GROUP) on local interface",
     &SockScripter::Drop6},
    {"listen", nullptr, "listen on TCP socket", &SockScripter::Listen},
    {"accept", nullptr, "accept on TCP socket", &SockScripter::Accept},
    {"sendto", "<send-to-ip>:<send-to-port>", "sendto(send_buf, ip, port)", &SockScripter::SendTo},
    {"send", nullptr, "send send_buf on a connected socket", &SockScripter::Send},
    {"recvfrom", nullptr, "recvfrom()", &SockScripter::RecvFrom},
    {"recvfrom-ping", nullptr, "recvfrom() and ping the packet back to the sender",
     &SockScripter::RecvFromPing},
    {"recv", nullptr, "recv() on a connected TCP socket", &SockScripter::Recv},
    {"recv-ping", nullptr,
     "recv() and ping back the packet on a connected TCP "
     "socket",
     &SockScripter::RecvPing},
    {"set-send-buf-hex", "\"xx xx ..\" ", "set send-buffer with hex values",
     &SockScripter::SetSendBufHex},
    {"set-send-buf-text", "\"<string>\" ", "set send-buffer with text chars",
     &SockScripter::SetSendBufText},
    {"sleep", "<sleep-secs>", "sleeps", &SockScripter::Sleep},
};

int print_socket_types() {
  for (const auto& socket_type : socket_types) {
    std::cout << socket_type.name << " ";
  }
  std::cout << std::endl;
  return 0;
}

int check_socket_type_has_proto(const char* stype) {
  for (const auto& socket_type : socket_types) {
    if (strcmp(stype, socket_type.name) == 0) {
      if (socket_type.arg) {
        return 0;
      }
      break;
    }
  }
  return 1;
}

int print_commands_list() {
  for (const auto& cmd : kCommands) {
    std::cout << cmd.name << " ";
  }
  std::cout << std::endl;
  return 0;
}

int check_command_has_args(const char* command) {
  for (const auto& cmd : kCommands) {
    if (strcmp(command, cmd.name) == 0) {
      if (cmd.opt_arg_descr) {
        return 0;
      }
      break;
    }
  }
  return 1;
}

int usage(const char* name) {
  std::stringstream socket_types_str;
  for (const auto& socket_type : socket_types) {
    socket_types_str << "    " << socket_type.name << " "
                     << (socket_type.arg ? socket_type.arg : "") << " : " << socket_type.help_str
                     << "\n";
  }

  std::stringstream cmds_str;
  for (const auto& cmd : kCommands) {
    cmds_str << "    " << cmd.name << " " << (cmd.opt_arg_descr ? cmd.opt_arg_descr : "") << " : "
             << cmd.help_str << "\n";
  }

  fprintf(stderr,
          "\nUsage: %s [-h] [-s] [-p <proto>] [-c] [-a <cmd>] "
          " {socket-type} {socket-cmds}\n"
          "    -h : this help message\n"
          "    -s : prints available socket-types (for bash completion)\n"
          "    -p <proto> : returns 0 if the given socket-type has a parameter\n"
          "    -c : prints available commands (for bash completion)\n"
          "    -a <cmd> : returns 0 if the given command has a parameter\n"
          "\n"
          "  socket-type is one of the following:\n%s\n"
          "  socket-cmd is one of the following:\n%s\n"
          "  <local-intf-id> is an integer prefixed by '%%', e.g. '%%1'\n"
          "  <local-intf-Addr> is of the format [<IP>][<ID>], e.g. '192.168.1.166',"
          "'192.168.1.166%%2', '%%2'\n\n"
          "  A command can be repeated by wrapping it in the following structure:\n"
          "    {<cmd>}[N=<n>][T=<t>]\n"
          "        <n> is the number of repeats (default: n=1)\n"
          "        <t> is the delay in ms between commands (default: t=1000)\n\n"
          "  Examples:\n    ------\n"
          "    Joining the multicast group 224.0.0.120 on local-IP "
          "192.168.1.99, bind to 224.0.0.120:2000,\n"
          "    and receive a two packets.\n"
          "       %s udp join4 224.0.0.120-192.168.1.99 bind 224.0.0.120:2000 "
          "{recvfrom}N=2\n"
          "\n"
          "    Send two packets, 50ms apart, to the multicast group 224.0.0.120:2000 "
          "from the local interface 192.168.1.166\n"
          "      %s udp set-mcast-if4 192.168.1.166 {sendto}N=2T=50 224.0.0.120:2000\n"
          "\n\n",
          name, socket_types_str.str().c_str(), cmds_str.str().c_str(), name, name);
  return 99;
}

int SockScripter::Execute(int argc, char* const argv[]) {
  optind = 0;
  int opt;
  while ((opt = getopt(argc, argv, "hsp:ca:")) != -1) {
    switch (opt) {
      case 's':
        return print_socket_types();
      case 'p':
        return check_socket_type_has_proto(optarg);
      case 'c':
        return print_commands_list();
      case 'a':
        return check_command_has_args(optarg);
      case 'h':
      default:
        return usage(argv[0]);
    }
  }

  if (optind >= argc) {
    return usage(argv[0]);
  }

  bool found = false;
  for (const auto& socket_type : socket_types) {
    if (strcmp(argv[optind], socket_type.name) == 0) {
      found = true;
      int proto = 0;
      if (socket_type.arg) {
        optind++;
        if (optind >= argc) {
          fprintf(stderr, "Error-Need protocol# for RAW socket!\n\n");
          return -1;
        }
        if (!str2int(argv[optind], &proto)) {
          fprintf(stderr, "Error-Invalid protocol# (%s) for RAW socket!\n\n", argv[optind]);
          return -1;
        }
      }
      if (!Open(socket_type.domain, socket_type.type, proto)) {
        return -1;
      }
      break;
    }
  }
  if (!found) {
    fprintf(stderr, "Error-first parameter needs to be socket type:");
    print_socket_types();
  }

  optind++;
  while (optind < argc) {
    TestRepeatCfg cfg;
    const char* cmd_arg;
    if (argv[optind][0] == '{') {
      if (!cfg.Parse(argv[optind])) {
        return -1;
      }
      cmd_arg = cfg.command.c_str();
    } else {
      cmd_arg = argv[optind];
    }

    bool found = false;
    for (const auto& cmd : kCommands) {
      if (strcmp(cmd_arg, cmd.name) == 0) {
        found = true;
        optind++;
        char* arg = nullptr;
        if (cmd.opt_arg_descr) {
          if (optind < argc) {
            arg = argv[optind];
            optind++;
          } else {
            fprintf(stderr, "Missing argument %s for %s!\n\n", cmd.opt_arg_descr, cmd.name);
            return -1;
          }
        }
        for (int i = 0; i < cfg.repeat_count; i++) {
          auto handler = cmd.handler;
          if (!(this->*(handler))(arg)) {
            return -1;
          }
          usleep(1000 * cfg.delay_ms);
        }
        break;
      }
    }
    if (!found) {
      fprintf(stderr, "Error: Cannot find command '%s'!\n\n", argv[optind]);
      return -1;
    }
  }
  return 0;
}

bool SockScripter::Open(int domain, int type, int proto) {
  const char* domain_str = (domain == AF_INET) ? "IPv4-" : "IPv6-";
  sockfd_ = api_->socket(domain, type, proto);
  if (sockfd_ < 0) {
    LOG(ERROR) << "Error-Opening " << domain_str << GetTypeName(type) << " socket "
               << "(proto:" << proto << ") "
               << "failed-[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "Opened " << domain_str << GetTypeName(type) << " socket "
            << "(proto:" << proto << ") fd:" << sockfd_;
  return true;
}

bool SockScripter::Close(char* arg) {
  if (sockfd_ >= 0) {
    api_->close(sockfd_);
    LOG(INFO) << "Closed socket-fd:" << sockfd_;
    sockfd_ = -1;
  }
  if (prev_sock_fd_ >= 0) {
    sockfd_ = prev_sock_fd_;
    prev_sock_fd_ = -1;
  }
  return true;
}

bool SockScripter::SetBroadcast(char* arg) {
  int flag;
  if (!getFlagInt(arg, &flag)) {
    LOG(ERROR) << "Error: Invalid broadcast flag='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(SOL_SOCKET, SO_BROADCAST, flag);
}

bool SockScripter::LogBroadcast(char* arg) { LOG_SOCK_OPT_VAL(SOL_SOCKET, SO_BROADCAST, int); }

bool SockScripter::SetReuseaddr(char* arg) {
  int flag;
  if (!getFlagInt(arg, &flag)) {
    LOG(ERROR) << "Error: Invalid reuseaddr flag='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(SOL_SOCKET, SO_REUSEADDR, flag);
}

bool SockScripter::SetReuseport(char* arg) {
  int flag;
  if (!getFlagInt(arg, &flag)) {
    LOG(ERROR) << "Error: Invalid reuseport flag='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(SOL_SOCKET, SO_REUSEPORT, flag);
}

bool SockScripter::LogReuseaddr(char* arg) { LOG_SOCK_OPT_VAL(SOL_SOCKET, SO_REUSEADDR, int); }

bool SockScripter::LogReuseport(char* arg) { LOG_SOCK_OPT_VAL(SOL_SOCKET, SO_REUSEPORT, int); }

bool SockScripter::SetIpUnicastTTL(char* arg) {
  int ttl;
  if (!str2int(arg, &ttl) || ttl < 0) {
    LOG(ERROR) << "Error: Invalid unicast TTL='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(IPPROTO_IP, IP_TTL, ttl);
}

bool SockScripter::SetIpUnicastHops(char* arg) {
  int hops;
  if (!str2int(arg, &hops) || hops < 0) {
    LOG(ERROR) << "Error: Invalid unicast hops='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_UNICAST_HOPS, hops);
}

bool SockScripter::LogIpUnicastHops(char* arg) {
  LOG_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_UNICAST_HOPS, int);
}

bool SockScripter::SetIpMcastTTL(char* arg) {
  int ttl;
  if (!str2int(arg, &ttl) || ttl < 0) {
    LOG(ERROR) << "Error: Invalid mcast TTL='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(IPPROTO_IP, IP_MULTICAST_TTL, ttl);
}

bool SockScripter::LogIpMcastTTL(char* arg) { LOG_SOCK_OPT_VAL(IPPROTO_IP, IP_MULTICAST_TTL, int); }

bool SockScripter::LogIpUnicastTTL(char* arg) { LOG_SOCK_OPT_VAL(IPPROTO_IP, IP_TTL, int); }

bool SockScripter::SetIpMcastLoop4(char* arg) {
  int flag;
  if (!getFlagInt(arg, &flag)) {
    LOG(ERROR) << "Error: Invalid loop4 flag='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(IPPROTO_IP, IP_MULTICAST_LOOP, flag);
}

bool SockScripter::LogIpMcastLoop4(char* arg) {
  LOG_SOCK_OPT_VAL(IPPROTO_IP, IP_MULTICAST_LOOP, int);
}

bool SockScripter::SetIpMcastHops(char* arg) {
  int hops;
  if (!str2int(arg, &hops) || hops < 0) {
    LOG(ERROR) << "Error: Invalid mcast hops='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_MULTICAST_HOPS, hops);
}

bool SockScripter::LogIpMcastHops(char* arg) {
  LOG_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_MULTICAST_HOPS, int);
}

bool SockScripter::SetIpV6Only(char* arg) {
  int flag;
  if (!getFlagInt(arg, &flag)) {
    LOG(ERROR) << "Error: Invalid ipv6-only flag='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_V6ONLY, flag);
}

bool SockScripter::LogIpV6Only(char* arg) { LOG_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_V6ONLY, int); }

bool SockScripter::SetIpMcastLoop6(char* arg) {
  int flag;
  if (!getFlagInt(arg, &flag)) {
    LOG(ERROR) << "Error: Invalid loop6 flag='" << arg << "'!";
    return false;
  }
  SET_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_MULTICAST_LOOP, flag);
}

bool SockScripter::LogIpMcastLoop6(char* arg) {
  LOG_SOCK_OPT_VAL(IPPROTO_IPV6, IPV6_MULTICAST_LOOP, int);
}

bool SockScripter::SetBindToDevice(char* arg) {
#ifdef SO_BINDTODEVICE
  if (api_->setsockopt(sockfd_, SOL_SOCKET, SO_BINDTODEVICE, arg,
                       static_cast<socklen_t>(strlen(arg))) < 0) {
    LOG(ERROR) << "Error-Setting SO_BINDTODEVICE failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "Set SO_BINDTODEVICE to " << arg;
  return true;
#else
  LOG(ERROR) << "SO_BINDTODEVICE not defined in this platform";
  return false;
#endif
}

bool SockScripter::LogBindToDevice(char* arg) {
#ifdef SO_BINDTODEVICE
  char name[IFNAMSIZ] = {};
  socklen_t name_len = sizeof(name);
  if (api_->getsockopt(sockfd_, SOL_SOCKET, SO_BINDTODEVICE, name, &name_len) < 0) {
    LOG(ERROR) << "Error-Getting SO_BINDTODEVICE failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "SO_BINDTODEVICE is set to " << name;
  return true;
#else
  LOG(ERROR) << "SO_BINDTODEVICE not defined in this platform";
  return false;
#endif
}

bool SockScripter::SetIpMcastIf4(char* arg) {
  LocalIfAddr if_addr;
  if (!if_addr.Set(arg)) {
    return false;
  }
  if (!if_addr.IsSet()) {
    LOG(ERROR) << "Error-No IPv4 address or local interface id given for "
               << "IP_MULTICAST_IF";
    return false;
  }

  struct ip_mreqn mreq;
  memset(&mreq, 0, sizeof(mreq));
  // mreq.imr_multiaddr is not set
  if (if_addr.HasAddr4()) {
    mreq.imr_address = if_addr.GetAddr4();
  }
  if (if_addr.HasId()) {
    mreq.imr_ifindex = if_addr.GetId();
  }
  if (api_->setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0) {
    LOG(ERROR) << "Error-Setting IP_MULTICAST_IF failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "Set IP_MULTICAST_IF to " << if_addr.Name();
  return true;
}

bool SockScripter::LogIpMcastIf4(char* arg) {
  struct in_addr addr;
  memset(&addr, 0, sizeof(addr));
  socklen_t addr_len = sizeof(addr);
  if (api_->getsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_IF, &addr, &addr_len) < 0) {
    LOG(ERROR) << "Error-Getting IP_MULTICAST_IF failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LocalIfAddr if_addr;
  if (!if_addr.Set(&addr, addr_len)) {
    return false;
  }
  LOG(INFO) << "IP_MULTICAST_IF is set to " << if_addr.Name();
  return true;
}

bool SockScripter::SetIpMcastIf6(char* arg) {
  LocalIfAddr if_addr;
  if (!if_addr.Set(arg)) {
    return false;
  }
  if (!if_addr.HasId()) {
    LOG(ERROR) << "Error-No local interface ID given for IPV6_MULTICAST_IF";
    return false;
  }

  int id = if_addr.GetId();
  if (api_->setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_IF, &id, sizeof(id)) < 0) {
    LOG(ERROR) << "Error-Setting IPV6_MULTICAST_IF to " << id << " failed-[" << errno << "]"
               << strerror(errno);
    return false;
  }
  LOG(INFO) << "Set IPV6_MULTICAST_IF to " << id;
  return true;
}

bool SockScripter::LogIpMcastIf6(char* arg) {
  int id = -1;
  socklen_t id_len = sizeof(id);
  if (api_->getsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_IF, &id, &id_len) < 0) {
    LOG(ERROR) << "Error-Getting IPV6_MULTICAST_IF failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "IPV6_MULTICAST_IF is set to " << id;
  return true;
}

bool SockScripter::LogBoundToAddress(char* arg) {
  struct sockaddr* addr = GetSockAddrStorage();
  socklen_t addr_len = sizeof(sockaddr_store_);

  if (api_->getsockname(sockfd_, addr, &addr_len) < 0) {
    LOG(ERROR) << "Error-Calling getsockname failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  SockAddrIn bound_addr;
  bound_addr.Set(addr, addr_len);
  LOG(INFO) << "Bound to " << bound_addr.Name();
  return true;
}

bool SockScripter::LogPeerAddress(char* arg) {
  struct sockaddr* addr = GetSockAddrStorage();
  socklen_t addr_len = sizeof(sockaddr_store_);

  if (api_->getpeername(sockfd_, addr, &addr_len) < 0) {
    LOG(ERROR) << "Error-Calling getpeername failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  SockAddrIn bound_addr;
  bound_addr.Set(addr, addr_len);
  LOG(INFO) << "Connected to " << bound_addr.Name();
  return true;
}

bool SockScripter::Bind(char* arg) {
  SockAddrIn bind_addr;
  if (!bind_addr.Set(arg)) {
    return false;
  }

  LOG(INFO) << "Bind(fd:" << sockfd_ << ") to " << bind_addr.Name();

  struct sockaddr* addr = GetSockAddrStorage();
  int addr_len = sizeof(sockaddr_store_);
  if (!bind_addr.Fill(addr, &addr_len)) {
    return false;
  }

  if (api_->bind(sockfd_, addr, addr_len) < 0) {
    LOG(ERROR) << "Error-Bind(fd:" << sockfd_ << ") failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  return LogBoundToAddress(nullptr);
}

bool SockScripter::Connect(char* arg) {
  SockAddrIn connect_addr;
  if (!connect_addr.Set(arg)) {
    return false;
  }

  LOG(INFO) << "Connect(fd:" << sockfd_ << ") to " << connect_addr.Name();

  struct sockaddr* addr = GetSockAddrStorage();
  int addr_len = sizeof(sockaddr_store_);
  if (!connect_addr.Fill(addr, &addr_len, true)) {
    return false;
  }

  if (api_->connect(sockfd_, addr, addr_len) < 0) {
    LOG(ERROR) << "Error-Connect(fd:" << sockfd_ << ") failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  return LogBoundToAddress(nullptr);
}

bool SockScripter::Disconnect(char* arg) {
  LOG(INFO) << "Disconnect(fd:" << sockfd_ << ")";

  struct sockaddr_storage addr = {};
  addr.ss_family = AF_UNSPEC;
  if (api_->connect(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr.ss_family)) < 0) {
    LOG(ERROR) << "Error-Disconnect(fd:" << sockfd_ << ") failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  return LogBoundToAddress(nullptr);
}

bool SockScripter::JoinOrDrop4(const char* func, char* arg, int optname, const char* optname_str) {
  if (arg == nullptr) {
    LOG(ERROR) << "Called " << func << " with arg == nullptr!";
    return false;
  }

  std::string saved_arg(arg);
  char* mcast_ip_str = strtok(arg, "-");
  char* ip_id_str = strtok(nullptr, "-");

  if (mcast_ip_str == nullptr || ip_id_str == nullptr) {
    LOG(ERROR) << "Error-" << func << " got arg='" << saved_arg << "', "
               << "needs to be <mcast-ip>-{<local-intf-ip>|<local-intf-id>}";
    return false;
  }

  InAddr mcast_addr;
  if (!mcast_addr.Set(mcast_ip_str) || !mcast_addr.IsAddr4()) {
    LOG(ERROR) << "Error-" << func << " got invalid mcast address='" << mcast_ip_str << "'!";
    return false;
  }

  LocalIfAddr if_addr;
  if (!if_addr.Set(ip_id_str)) {
    LOG(ERROR) << "Error-" << func << " got invalid interface='" << ip_id_str << "'!";
    return false;
  }

  struct ip_mreqn mreq;
  memset(&mreq, 0, sizeof(mreq));
  mreq.imr_multiaddr = mcast_addr.GetAddr4();
  if (if_addr.HasAddr4()) {
    mreq.imr_address = if_addr.GetAddr4();
  }
  if (if_addr.HasId()) {
    mreq.imr_ifindex = if_addr.GetId();
  }

  if (api_->setsockopt(sockfd_, IPPROTO_IP, optname, &mreq, sizeof(mreq)) < 0) {
    LOG(ERROR) << "Error-Setting " << optname_str << " failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "Set " << optname_str << " for " << mcast_addr.Name() << " on " << if_addr.Name()
            << ".";

  return true;
}

bool SockScripter::Join4(char* arg) {
  return JoinOrDrop4(__FUNCTION__, arg, IP_ADD_MEMBERSHIP, "IP_ADD_MEMBERSHIP");
}

bool SockScripter::Drop4(char* arg) {
  return JoinOrDrop4(__FUNCTION__, arg, IP_DROP_MEMBERSHIP, "IP_DROP_MEMBERSHIP");
}

bool SockScripter::JoinOrDrop6(const char* func, char* arg, int optname, const char* optname_str) {
  if (arg == nullptr) {
    LOG(ERROR) << "Called " << func << " with arg == nullptr!";
    return false;
  }

  std::string saved_arg(arg);
  char* mcast_ip_str = strtok(arg, "-");
  char* id_str = strtok(nullptr, "-");
  if (mcast_ip_str == nullptr || id_str == nullptr) {
    LOG(ERROR) << "Error-" << func << " got arg='" << saved_arg << "'', "
               << "needs to be '<mcast-ip>-<local-intf-id>'";
    return false;
  }

  InAddr mcast_addr;
  if (!mcast_addr.Set(mcast_ip_str) || !mcast_addr.IsAddr6()) {
    LOG(ERROR) << "Error-" << func << " got invalid mcast address='" << mcast_ip_str << "'!";
    return false;
  }

  LocalIfAddr if_addr;
  if (!if_addr.Set(id_str)) {  // || !if_addr.HasId()) {
    LOG(ERROR) << "Error-" << func << " got invalid interface='" << id_str << "'!";
    return false;
  }

  struct ipv6_mreq mreq;
  memset(&mreq, 0, sizeof(mreq));
  mreq.ipv6mr_multiaddr = mcast_addr.GetAddr6();
  mreq.ipv6mr_interface = if_addr.GetId();

  if (api_->setsockopt(sockfd_, IPPROTO_IPV6, optname, &mreq, sizeof(mreq)) < 0) {
    LOG(ERROR) << "Error-Setting " << optname_str << " failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "Set " << optname_str << " for " << mcast_addr.Name() << " on " << if_addr.Name()
            << ".";

  return true;
}

bool SockScripter::Join6(char* arg) {
  // Note that IPV6_JOIN_GROUP and IPV6_ADD_MEMBERSHIP map to the same optname integer code, so we
  // can pick either one. We go with IPV6_JOIN_GROUP due to wider support.
  return JoinOrDrop6(__FUNCTION__, arg, IPV6_JOIN_GROUP, "IPV6_JOIN_GROUP");
}

bool SockScripter::Drop6(char* arg) {
  return JoinOrDrop6(__FUNCTION__, arg, IPV6_LEAVE_GROUP, "IPV6_LEAVE_GROUP");
}

bool SockScripter::Listen(char* arg) {
  if (api_->listen(sockfd_, 1) < 0) {
    LOG(ERROR) << "Error-listen(fd:" << sockfd_ << ") failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  return true;
}

bool SockScripter::Accept(char* arg) {
  struct sockaddr* addr = GetSockAddrStorage();
  socklen_t addr_len = sizeof(sockaddr_store_);

  LOG(INFO) << "Accepting(fd:" << sockfd_ << ")...";

  int fd = api_->accept(sockfd_, addr, &addr_len);
  if (fd < 0) {
    LOG(ERROR) << "Error-Accept(fd:" << sockfd_ << ") failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }

  SockAddrIn remote_addr;
  if (!remote_addr.Set(addr, addr_len)) {
    return false;
  }

  LOG(INFO) << "  accepted(fd:" << sockfd_ << ") new connection "
            << "(fd:" << fd << ") from " << remote_addr.Name();

  // Switch to the newly created connection, remember the previous one.
  prev_sock_fd_ = sockfd_;
  sockfd_ = fd;
  return true;
}

bool SockScripter::SendTo(char* arg) {
  SockAddrIn dst;
  if (!dst.Set(arg)) {
    return false;
  }

  auto snd_buf = snd_buf_gen_.GetSndStr();

  LOG(INFO) << "Sending [" << snd_buf.length() << "]='" << snd_buf << "' on fd:" << sockfd_
            << " to " << dst.Name();

  struct sockaddr* addr = GetSockAddrStorage();
  int addr_len = sizeof(sockaddr_store_);
  if (!dst.Fill(addr, &addr_len)) {
    return false;
  }

  ssize_t sent =
      api_->sendto(sockfd_, snd_buf.c_str(), snd_buf.length(), snd_flags_, addr, addr_len);
  if (sent < 0) {
    LOG(ERROR) << "Error-sendto(fd:" << sockfd_ << ") failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "Sent [" << sent << "] on fd:" << sockfd_;
  return true;
}

bool SockScripter::Send(char* arg) {
  auto snd_buf = snd_buf_gen_.GetSndStr();

  LOG(INFO) << "Sending [" << snd_buf.length() << "]='" << snd_buf << "' on fd:" << sockfd_;

  ssize_t sent = api_->send(sockfd_, snd_buf.c_str(), snd_buf.length(), snd_flags_);
  if (sent < 0) {
    LOG(ERROR) << "Error-send(fd:" << sockfd_ << ") failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }
  LOG(INFO) << "Sent [" << sent << "] on fd:" << sockfd_;
  return true;
}

bool SockScripter::RecvFromInternal(bool ping) {
  struct sockaddr* addr = GetSockAddrStorage();
  socklen_t addr_len = sizeof(sockaddr_store_);

  LOG(INFO) << "RecvFrom(fd:" << sockfd_ << ")...";

  memset(recv_buf_, 0, sizeof(recv_buf_));
  ssize_t recvd =
      api_->recvfrom(sockfd_, recv_buf_, sizeof(recv_buf_) - 1, recv_flags_, addr, &addr_len);
  if (recvd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      LOG(INFO) << "  returned EAGAIN or EWOULDBLOCK!";
    } else {
      LOG(ERROR) << "  Error-recvfrom(fd:" << sockfd_ << ") failed-"
                 << "[" << errno << "]" << strerror(errno);
    }
    return false;
  }

  SockAddrIn src_addr;
  if (!src_addr.Set(addr, addr_len)) {
    return false;
  }

  LOG(INFO) << "  received(fd:" << sockfd_ << ") [" << recvd << "]'" << recv_buf_ << "' "
            << "from " << src_addr.Name();
  if (ping) {
    if (api_->sendto(sockfd_, recv_buf_, recvd, snd_flags_, addr, addr_len) < 0) {
      LOG(ERROR) << "Error-sendto(fd:" << sockfd_ << ") failed-"
                 << "[" << errno << "]" << strerror(errno);
      return false;
    }
  }
  return true;
}

bool SockScripter::RecvFrom(char* arg) { return RecvFromInternal(false /* ping */); }

bool SockScripter::RecvFromPing(char* arg) { return RecvFromInternal(true /* ping */); }

int SockScripter::RecvInternal(bool ping) {
  LOG(INFO) << "Recv(fd:" << sockfd_ << ") ...";
  LogBoundToAddress(nullptr);
  LogPeerAddress(nullptr);

  memset(recv_buf_, 0, sizeof(recv_buf_));
  ssize_t recvd = api_->recv(sockfd_, recv_buf_, sizeof(recv_buf_) - 1, recv_flags_);
  if (recvd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      LOG(INFO) << "  returned EAGAIN or EWOULDBLOCK!";
    } else {
      LOG(ERROR) << "  Error-recv(fd:" << sockfd_ << ") failed-"
                 << "[" << errno << "]" << strerror(errno);
    }
    return false;
  }
  LOG(INFO) << "  received(fd:" << sockfd_ << ") [" << recvd << "]'" << recv_buf_ << "' ";
  if (ping) {
    if (api_->send(sockfd_, recv_buf_, recvd, recv_flags_) < 0) {
      LOG(ERROR) << "Error-send(fd:" << sockfd_ << ") failed-"
                 << "[" << errno << "]" << strerror(errno);
      return false;
    }
  }
  return true;
}

bool SockScripter::Recv(char* arg) { return RecvInternal(false /* ping */); }

bool SockScripter::RecvPing(char* arg) { return RecvInternal(true /* ping */); }

bool SockScripter::SetSendBufHex(char* arg) { return snd_buf_gen_.SetSendBufHex(arg); }

bool SockScripter::SetSendBufText(char* arg) { return snd_buf_gen_.SetSendBufText(arg); }

bool SockScripter::Sleep(char* arg) {
  int sleeptime;
  if (!str2int(arg, &sleeptime) || sleeptime < 0) {
    LOG(ERROR) << "Error: Invalid sleeptime='" << arg << "'!";
    return false;
  }
  LOG(INFO) << "Sleep for " << sleeptime << " secs...";
  if (sleeptime > 0) {
    sleep(sleeptime);
  }
  LOG(INFO) << "Wake up!";
  return true;
}

struct sockaddr* SockScripter::GetSockAddrStorage() {
  memset(&sockaddr_store_, 0, sizeof(sockaddr_store_));
  return reinterpret_cast<struct sockaddr*>(&sockaddr_store_);
}

std::string SendBufferGenerator::GetSndStr() {
  switch (mode_) {
    case COUNTER_TEXT: {
      std::string ret_str = snd_str_;
      auto cnt_specifier = ret_str.find("%c");
      if (cnt_specifier != std::string::npos) {
        ret_str.replace(cnt_specifier, 2, std::to_string(counter_));
      }
      counter_++;
      return ret_str;
    }
    case STATIC_TEXT:
    default:
      return snd_str_;
  }
}

bool SendBufferGenerator::SetSendBufHex(const char* arg) {
  std::string_view str(arg, strlen(arg));
  std::stringstream ss;
  while (str.length()) {
    auto f = str.front();
    uint8_t v;
    // always allow any number of spaces or commas to happen
    if (f == ' ' || f == ',') {
      str = str.substr(1);
      continue;
    } else if (str.length() < 2 ||
               !fxl::StringToNumberWithError(str.substr(0, 2), &v, fxl::Base::k16)) {
      // trailing character at the end that we don't recognize or failed to parse number
      return false;
    }
    str = str.substr(2);
    ss.put(v);
  }
  snd_str_ = ss.str();
  return true;
}

bool SendBufferGenerator::SetSendBufText(const char* arg) {
  snd_str_ = arg;
  return true;
}
