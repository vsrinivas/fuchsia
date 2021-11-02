// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define DEBUG_LOGGING 0

#include "tcp.h"

#include <log.h>
#include <stdio.h>
#include <string.h>
#include <xefi.h>

#include <efi/boot-services.h>

#include "compiler.h"
#include "inet6.h"

static efi_guid kTcp6ServiceBindingProtocolGuid = EFI_TCP6_SERVICE_BINDING_PROTOCOL_GUID;
static efi_guid kTcp6ProtocolGuid = EFI_TCP6_PROTOCOL_GUID;

// Dumps the TCP state for debugging purposes.
static void __UNUSED dump_state(efi_tcp6_protocol* protocol) {
  efi_tcp6_connection_state connection_state;
  efi_tcp6_option option = {};
  efi_tcp6_config_data config_data = {.ControlOption = &option};
  efi_ip6_mode_data mode_data = {};
  efi_managed_network_config_data managed_network_config_data = {};
  efi_simple_network_mode simple_network_mode = {};
  efi_status status = protocol->GetModeData(protocol, &connection_state, &config_data, &mode_data,
                                            &managed_network_config_data, &simple_network_mode);
  if (status != EFI_SUCCESS) {
    ELOG_S(status, "Failed to fetch TCP6 mode data");
    return;
  }

  char ip_buffer[IP6TOAMAX];
  LOG("Connection state: %d", connection_state);
  LOG("Config data:");
  LOG("  TrafficClass: %u", config_data.TrafficClass);
  LOG("  HopLimit: %u", config_data.HopLimit);
  LOG("  AccessPoint:");
  LOG("    StationAddress: %s", ip6toa(ip_buffer, &config_data.AccessPoint.StationAddress));
  LOG("    StationPort: %d", config_data.AccessPoint.StationPort);
  LOG("    RemoteAddress: %s", ip6toa(ip_buffer, &config_data.AccessPoint.RemoteAddress));
  LOG("    RemotePort: %d", config_data.AccessPoint.RemotePort);
  LOG("    ActiveFlag: %d", config_data.AccessPoint.ActiveFlag);
  LOG("  ControlOption:");
  LOG("    ReceiveBufferSize: %u", config_data.ControlOption->ReceiveBufferSize);
  LOG("    SendBufferSize: %u", config_data.ControlOption->SendBufferSize);
  LOG("    MaxSynBackLog: %u", config_data.ControlOption->MaxSynBackLog);
  LOG("    ConnectionTimeout: %u", config_data.ControlOption->ConnectionTimeout);
  LOG("    DataRetries: %u", config_data.ControlOption->DataRetries);
  LOG("    FinTimeout: %u", config_data.ControlOption->FinTimeout);
  LOG("    TimeWaitTimeout: %u", config_data.ControlOption->TimeWaitTimeout);
  LOG("    KeepAliveProbes: %u", config_data.ControlOption->KeepAliveProbes);
  LOG("    KeepAliveTime: %u", config_data.ControlOption->KeepAliveTime);
  LOG("    KeepAliveInterval: %u", config_data.ControlOption->KeepAliveInterval);
  LOG("    EnableNagle: %d", config_data.ControlOption->EnableNagle);
  LOG("    EnableTimeStamp: %d", config_data.ControlOption->EnableTimeStamp);
  LOG("    EnableWindowScaling: %d", config_data.ControlOption->EnableWindowScaling);
  LOG("    EnableSelectiveAck: %d", config_data.ControlOption->EnableSelectiveAck);
  LOG("    EnablePathMtuDiscovery: %d", config_data.ControlOption->EnablePathMtuDiscovery);
  // We could dump the rest of the structs here as well if useful, but for now
  // this contains most of the relevant info.
}

// Converts the given efi_status to the more generic TCP6_RESULT code.
static tcp6_result status_to_tcp6_result(efi_status status) {
  switch (status) {
    case EFI_SUCCESS:
      return TCP6_RESULT_SUCCESS;
    case EFI_NOT_READY:
      return TCP6_RESULT_PENDING;
    case EFI_CONNECTION_FIN:
      FALLTHROUGH;
    case EFI_CONNECTION_RESET:
      DLOG_S(status, "TCP6 client has disconnected");
      return TCP6_RESULT_DISCONNECTED;
    default:
      ELOG_S(status, "TCP6 error");
      return TCP6_RESULT_ERROR;
  }
}

// Closes the token's event and resets the state.
static void reset_token(efi_boot_services* boot_services, efi_tcp6_completion_token* token) {
  if (token->Event) {
    efi_status status = boot_services->CloseEvent(token->Event);
    if (status != EFI_SUCCESS) {
      // Log a warning, but keep going. Failure to close essentially means
      // whatever we were trying to close is already gone.
      WLOG_S(status, "Failed to close TCP event");
    }
    token->Event = NULL;
  }
  token->Status = EFI_SUCCESS;
}

// Checks if the completion token is done.
//
// On success or error, resets the token and returns the resulting status.
// If the event is still pending, returns TCP6_RESULT_PENDING.
static tcp6_result check_token(efi_boot_services* boot_services, efi_tcp6_completion_token* token) {
  efi_status status = boot_services->CheckEvent(token->Event);

  // If the event completed, return the final token status.
  // Do this first so we don't lose the token status when we reset it.
  if (status == EFI_SUCCESS) {
    status = token->Status;
  }

  // Anything except pending, reset the event since we're done with it.
  if (status != EFI_NOT_READY) {
    reset_token(boot_services, token);
  }

  return status_to_tcp6_result(status);
}

tcp6_result tcp6_open(tcp6_socket* socket, efi_boot_services* boot_services,
                      const efi_ipv6_addr* address, uint16_t port) {
  memset(socket, 0, sizeof(*socket));

  socket->boot_services = boot_services;

  // TCP uses the service binding protocol mechanism, so we have to open the
  // binding protocol first then open the actual protocol child.
  DLOG("Locating TCP6 binding handle");
  efi_handle* handles;
  size_t num_handles = 0;
  efi_status status = boot_services->LocateHandleBuffer(
      ByProtocol, &kTcp6ServiceBindingProtocolGuid, NULL, &num_handles, &handles);
  if (status != EFI_SUCCESS) {
    ELOG_S(status, "Failed to locate any TCP handles");
    return TCP6_RESULT_ERROR;
  }

  if (num_handles == 0) {
    ELOG("No TCP service handles found");
    boot_services->FreePool(handles);
    return TCP6_RESULT_ERROR;
  } else if (num_handles > 1) {
    // To keep things simple for now, just always take the first handle. We'll
    // probably want to improve this in the future.
    WLOG("Found %zu TCP service handles, but only using the first", num_handles);
  }
  socket->binding_handle = handles[0];
  boot_services->FreePool(handles);

  DLOG("Opening TCP6 binding protocol");
  status = boot_services->OpenProtocol(socket->binding_handle, &kTcp6ServiceBindingProtocolGuid,
                                       (void**)&socket->binding_protocol, gImg, NULL,
                                       EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (status != EFI_SUCCESS) {
    ELOG_S(status, "Failed to open TCP binding protocol");
    return TCP6_RESULT_ERROR;
  }

  DLOG("Creating TCP6 server handle");
  status = socket->binding_protocol->CreateChild(socket->binding_protocol, &socket->server_handle);
  if (status != EFI_SUCCESS) {
    ELOG_S(status, "Failed to create TCP child handle");
    tcp6_close(socket);
    return TCP6_RESULT_ERROR;
  }

  DLOG("Opening TCP6 server protocol");
  status = boot_services->OpenProtocol(socket->server_handle, &kTcp6ProtocolGuid,
                                       (void**)&socket->server_protocol, gImg, NULL,
                                       EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (status != EFI_SUCCESS) {
    ELOG_S(status, "Failed to open TCP protocol");
    tcp6_close(socket);
    return TCP6_RESULT_ERROR;
  }

  efi_tcp6_config_data config_data = {
      .TrafficClass = 0,  // Default forwarding, no congestion notification.
      .HopLimit = 0xFF,   // Maximum hop limit.
      .AccessPoint = {.StationAddress = *address,
                      .StationPort = port,
                      .RemoteAddress = {},
                      .RemotePort = 0,
                      .ActiveFlag = false},
      .ControlOption = NULL  // Use defaults.
  };

  char ip_buffer[IP6TOAMAX] __UNUSED;
  DLOG("Configuring TCP6 server for [%s]:%d",
       ip6toa(ip_buffer, &config_data.AccessPoint.StationAddress), port);
  status = socket->server_protocol->Configure(socket->server_protocol, &config_data);
  if (status != EFI_SUCCESS) {
    // Configure() will sometimes return EFI_INVALID_PARAMETER early on but
    // then succeed with the same parameters later, I think because any given
    // IP address will be invalid until the link is fully up. This is pretty
    // normal, so only debug log in this case to avoid spamming the console.
    if (status == EFI_INVALID_PARAMETER) {
      DLOG_S(status, "TCP configure failed - link not up yet?");
    } else {
      ELOG_S(status, "Failed to configure TCP protocol");
    }
    tcp6_close(socket);
    return TCP6_RESULT_ERROR;
  }

#if DEBUG_LOGGING
  DLOG("== TCP6 server state ==");
  dump_state(socket->server_protocol);
#endif  // DEBUG_LOGGING

  DLOG("TCP6 open success");
  return TCP6_RESULT_SUCCESS;
}

tcp6_result tcp6_accept(tcp6_socket* socket) {
  // Currently for simplicity we only support a single TCP client at a time.
  if (socket->client_protocol != NULL) {
    ELOG("A TCP client is already connected");
    return TCP6_RESULT_ERROR;
  }

  // If we don't have a server_event yet, start listening on this socket.
  if (socket->server_accept_token.CompletionToken.Event == NULL) {
    DLOG("Creating TCP6 listen event");
    efi_status status = socket->boot_services->CreateEvent(
        0, 0, NULL, NULL, &socket->server_accept_token.CompletionToken.Event);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "Failed to create TCP6 listen event");
      return TCP6_RESULT_ERROR;
    }

    DLOG("Accepting incoming TCP6 connections");
    status = socket->server_protocol->Accept(socket->server_protocol, &socket->server_accept_token);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "TCP accept failed");
      reset_token(socket->boot_services, &socket->server_accept_token.CompletionToken);
      return TCP6_RESULT_ERROR;
    }
  }

  tcp6_result result =
      check_token(socket->boot_services, &socket->server_accept_token.CompletionToken);
  if (result == TCP6_RESULT_SUCCESS) {
    DLOG("TCP6 client is ready");
    socket->client_handle = socket->server_accept_token.NewChildHandle;
    efi_status status = socket->boot_services->OpenProtocol(
        socket->client_handle, &kTcp6ProtocolGuid, (void**)&socket->client_protocol, gImg, NULL,
        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "Failed to open TCP client protocol");
      return TCP6_RESULT_ERROR;
    }

    // Lookup and print the client IP if we're debug logging.
    if (DEBUG_LOGGING) {
      efi_tcp6_config_data config_data = {};
      status = socket->client_protocol->GetModeData(socket->client_protocol, NULL, &config_data,
                                                    NULL, NULL, NULL);
      if (status != EFI_SUCCESS) {
        WLOG_S(status, "Failed to fetch new client IP");
      } else {
        char ip_buffer[IP6TOAMAX];
        LOG("New TCP client: %s", ip6toa(ip_buffer, &config_data.AccessPoint.RemoteAddress));
      }
    }
  }
  return result;
}

tcp6_result tcp6_read(tcp6_socket* socket, void* data, uint32_t size) {
  if (socket->client_protocol == NULL) {
    ELOG("No TCP client to read from");
    return TCP6_RESULT_ERROR;
  }

  // If there isn't a read in progress, start a new one.
  if (socket->read_token.CompletionToken.Event == NULL) {
    DLOG("Creating TCP6 read event");
    efi_status status = socket->boot_services->CreateEvent(
        0, 0, NULL, NULL, &socket->read_token.CompletionToken.Event);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "Failed to create TCP6 read event");
      return TCP6_RESULT_ERROR;
    }

    // Store the original read end so that we can internally handle partial
    // reads. The EFI documentation isn't clear whether drivers can give us
    // partial reads or not, so assume that they will.
    socket->read_end = (uint8_t*)data + size;
    socket->read_data.UrgentFlag = false;
    socket->read_data.DataLength = size;
    socket->read_data.FragmentCount = 1;
    socket->read_data.FragmentTable[0].FragmentBuffer = data;
    socket->read_data.FragmentTable[0].FragmentLength = size;
    socket->read_token.Packet.RxData = &socket->read_data;

    status = socket->client_protocol->Receive(socket->client_protocol, &socket->read_token);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "TCP read failed to start");
      reset_token(socket->boot_services, &socket->read_token.CompletionToken);
      return TCP6_RESULT_ERROR;
    }
  }

  // The interrupt rate can be pretty slow (10-20ms) which really hurts
  // performance on larger transfers, so manually poll whenever we're waiting
  // for a read.
  socket->client_protocol->Poll(socket->client_protocol);

  tcp6_result result = check_token(socket->boot_services, &socket->read_token.CompletionToken);
  if (result == TCP6_RESULT_SUCCESS) {
    DLOG("TCP6 read: %u bytes\n", socket->read_token.Packet.RxData->DataLength);

    // Check if we're done with the read.
    uint8_t* end = (uint8_t*)socket->read_token.Packet.RxData->FragmentTable[0].FragmentBuffer +
                   socket->read_token.Packet.RxData->FragmentTable[0].FragmentLength;
    if (end > socket->read_end) {
      // Only possible if the driver is misbehaving and gives us more data than
      // we asked for.
      ELOG("TCP driver returned more data than expected");
      return TCP6_RESULT_ERROR;
    }
    if (end < socket->read_end) {
      // Partial read - advance the buffers and read again.

      // 32-bit cast is safe because we calculated read_end by adding a 32-bit
      // size in the first place, so the difference must be < 32 bits.
      uint32_t remaining = (uint32_t)(socket->read_end - end);
      DLOG("TCP6 partial read; starting again on the next %u bytes", remaining);
      return tcp6_read(socket, end, remaining);
    }
  }
  return result;
}

tcp6_result tcp6_write(tcp6_socket* socket, const void* data, uint32_t size) {
  if (socket->client_protocol == NULL) {
    ELOG("No TCP client to write to");
    return TCP6_RESULT_ERROR;
  }

  // If there isn't a write in progress, start a new one.
  if (socket->write_token.CompletionToken.Event == NULL) {
    DLOG("Creating TCP6 write event");
    efi_status status = socket->boot_services->CreateEvent(
        0, 0, NULL, NULL, &socket->write_token.CompletionToken.Event);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "Failed to create TCP6 write event");
      return TCP6_RESULT_ERROR;
    }

    // For our usage, we nearly always want to send data out right away since
    // fastboot is a pretty sequential protocol, so just always push for now.
    socket->write_data.Push = true;
    socket->write_data.Urgent = false;
    socket->write_data.DataLength = size;
    socket->write_data.FragmentCount = 1;
    socket->write_data.FragmentTable[0].FragmentBuffer = (void*)data;
    socket->write_data.FragmentTable[0].FragmentLength = size;
    socket->write_token.Packet.TxData = &socket->write_data;

    status = socket->client_protocol->Transmit(socket->client_protocol, &socket->write_token);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "TCP write failed to start");
      reset_token(socket->boot_services, &socket->write_token.CompletionToken);
      return status_to_tcp6_result(status);
    }
  }

  // Manually poll to improve performance for large transfers.
  socket->client_protocol->Poll(socket->client_protocol);

  tcp6_result result = check_token(socket->boot_services, &socket->write_token.CompletionToken);
  if (result == TCP6_RESULT_SUCCESS) {
    DLOG("TCP6 write: %u bytes", socket->write_token.Packet.TxData->DataLength);

    // The EFI documentation indicates that writes do not update the TxData
    // struct, which means it must not support partial writes (since it would
    // be impossible to know how much data was written). Double-check here
    // so that we are alerted if this assumption ever fails.
    if (socket->write_token.Packet.TxData->DataLength != size) {
      ELOG("TCP6 write expected %u bytes, but only sent %u", size,
           socket->write_token.Packet.TxData->DataLength);
      return TCP6_RESULT_ERROR;
    }
  }
  return result;
}

static tcp6_result close_protocol(efi_boot_services* boot_services, efi_tcp6_protocol* protocol,
                                  efi_handle handle, efi_tcp6_close_token* close_token) {
  // No-op if we don't currently have a connected protocol.
  if (protocol == NULL) {
    return EFI_SUCCESS;
  }

  // If we don't have a close event yet, start the close.
  if (close_token->CompletionToken.Event == NULL) {
    DLOG("Creating TCP6 close event");
    efi_status status =
        boot_services->CreateEvent(0, 0, NULL, NULL, &close_token->CompletionToken.Event);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "Failed to create close event");
      return TCP6_RESULT_ERROR;
    }

    DLOG("Starting TCP6 close");
    status = protocol->Close(protocol, close_token);
    if (status != EFI_SUCCESS) {
      reset_token(boot_services, &close_token->CompletionToken);
      if (status == EFI_NOT_STARTED) {
        // NOT_STARTED is OK, means the protocol is already closed.
        return TCP6_RESULT_SUCCESS;
      }
      ELOG_S(status, "TCP Close() failed");
      return TCP6_RESULT_ERROR;
    }
  }

  tcp6_result result = check_token(boot_services, &close_token->CompletionToken);
  if (result == TCP6_RESULT_SUCCESS) {
    DLOG("TCP6 close finished");
    efi_status status = boot_services->CloseProtocol(handle, &kTcp6ProtocolGuid, gImg, NULL);
    if (status != EFI_SUCCESS) {
      // Warn but keep going, we'll just leak a protocol.
      WLOG_S(status, "Failed to close TCP6 protocol");
    }
  }
  return result;
}

tcp6_result tcp6_disconnect(tcp6_socket* socket) {
  DLOG("Closing TCP6 client protocol");
  tcp6_result result = close_protocol(socket->boot_services, socket->client_protocol,
                                      socket->client_handle, &socket->client_close_token);
  if (result == TCP6_RESULT_SUCCESS) {
    DLOG("TCP6 client disconnect complete");
    // We shouldn't need to do anything else to close the client_handle, once an
    // EFI handle has no open protocols it closes automatically.
    socket->client_handle = NULL;
    socket->client_protocol = NULL;
  }
  return result;
}

tcp6_result tcp6_close(tcp6_socket* socket) {
  // Close any connected client first, and wait until it's fully closed before
  // continuing on to tearing down the rest of the socket.
  //
  // We could probably close the server socket concurrently, but it's simpler
  // this way and works just as well for our purposes.
  tcp6_result result = tcp6_disconnect(socket);
  if (result != TCP6_RESULT_SUCCESS) {
    return result;
  }

  DLOG("Closing TCP6 server protocol");
  result = close_protocol(socket->boot_services, socket->server_protocol, socket->server_handle,
                          &socket->server_close_token);
  if (result != TCP6_RESULT_SUCCESS) {
    return result;
  }
  socket->server_protocol = NULL;

  DLOG("Closing TCP6 binding protocol and handles");
  if (socket->binding_protocol != NULL) {
    if (socket->server_handle != NULL) {
      efi_status status =
          socket->binding_protocol->DestroyChild(socket->binding_protocol, socket->server_handle);
      if (status != EFI_SUCCESS) {
        // Warn but keep going, we'll just leak a handle.
        WLOG_S(status, "Failed to destroy TCP6 server handle");
      }
      socket->server_handle = NULL;
    }

    efi_status status = socket->boot_services->CloseProtocol(
        socket->binding_handle, &kTcp6ServiceBindingProtocolGuid, gImg, NULL);
    if (status != EFI_SUCCESS) {
      // Warn but keep going, we'll just leak a protocol.
      WLOG_S(status, "Failed to close TCP6 binding protocol");
    }
    socket->binding_protocol = NULL;
  }
  socket->binding_handle = NULL;

  return TCP6_RESULT_SUCCESS;
}
