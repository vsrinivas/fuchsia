// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/process_explorer/utils.h"

#include <gtest/gtest.h>

namespace process_explorer {
namespace {

const zx_koid_t PROCESS_1 = 2476;
const zx_koid_t PROCESS_2 = 2254;
const zx_koid_t PROCESS_3 = 3458;

const zx_koid_t INTERRUPT_1 = 9650;
const zx_koid_t VMO_2 = 1189;
const zx_koid_t VMAR_3 = 17804;

const zx_koid_t CHANNEL_1[2] = {59782, 59783};
const zx_koid_t JOB_2[2] = {59792, 59793};
const zx_koid_t FIFO_3[2] = {14144, 14145};

const zx_koid_t CHANNEL_BETWEEN_1_AND_2[2] = {59797, 59798};
const zx_koid_t CHANNEL_BETWEEN_2_AND_3[2] = {39020, 39021};
const zx_koid_t SOCKET_BETWEEN_1_AND_3[2] = {40465, 40466};

void CreateObject(zx_obj_type_t type, zx_koid_t koid, zx_koid_t related_koid,
                  zx_koid_t peer_owner_koid, std::vector<KernelObject>* objects) {
  objects->push_back({.type = type,
                      .koid = koid,
                      .related_koid = related_koid,
                      .peer_owner_koid = peer_owner_koid});
}

void CreateProcess(zx_koid_t koid, std::string name, std::vector<KernelObject> objects,
                   std::vector<Process>* processes) {
  processes->push_back({.koid = koid, .name = name, .objects = objects});
}

std::vector<Process> InitialProcessesData() {
  std::vector<KernelObject> process_1_objects;
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_1_AND_2[0], CHANNEL_BETWEEN_1_AND_2[1], 0,
               &process_1_objects);
  CreateObject(ZX_OBJ_TYPE_SOCKET, SOCKET_BETWEEN_1_AND_3[0], SOCKET_BETWEEN_1_AND_3[1], 0,
               &process_1_objects);
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_1[0], CHANNEL_1[1], 0, &process_1_objects);
  CreateObject(ZX_OBJ_TYPE_INTERRUPT, INTERRUPT_1, 0, 0, &process_1_objects);

  std::vector<KernelObject> process_2_objects;
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_1_AND_2[1], CHANNEL_BETWEEN_1_AND_2[0], 0,
               &process_2_objects);
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_2_AND_3[0], CHANNEL_BETWEEN_2_AND_3[1], 0,
               &process_2_objects);
  CreateObject(ZX_OBJ_TYPE_JOB, JOB_2[0], JOB_2[1], 0, &process_2_objects);
  CreateObject(ZX_OBJ_TYPE_VMO, VMO_2, 0, 0, &process_2_objects);

  std::vector<KernelObject> process_3_objects;
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_2_AND_3[1], CHANNEL_BETWEEN_2_AND_3[0], 0,
               &process_3_objects);
  CreateObject(ZX_OBJ_TYPE_SOCKET, SOCKET_BETWEEN_1_AND_3[1], SOCKET_BETWEEN_1_AND_3[0], 0,
               &process_3_objects);
  CreateObject(ZX_OBJ_TYPE_FIFO, FIFO_3[0], FIFO_3[1], 0, &process_3_objects);
  CreateObject(ZX_OBJ_TYPE_VMAR, VMAR_3, 0, 0, &process_3_objects);

  std::vector<Process> processes;
  CreateProcess(PROCESS_1, "process1", process_1_objects, &processes);
  CreateProcess(PROCESS_2, "process2", process_2_objects, &processes);
  CreateProcess(PROCESS_3, "process3", process_3_objects, &processes);

  return processes;
}

std::vector<Process> ExpectedProcessesData() {
  std::vector<KernelObject> process_1_objects;
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_1_AND_2[0], CHANNEL_BETWEEN_1_AND_2[1],
               PROCESS_2, &process_1_objects);
  CreateObject(ZX_OBJ_TYPE_SOCKET, SOCKET_BETWEEN_1_AND_3[0], SOCKET_BETWEEN_1_AND_3[1], PROCESS_3,
               &process_1_objects);
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_1[0], CHANNEL_1[1], 0, &process_1_objects);
  CreateObject(ZX_OBJ_TYPE_INTERRUPT, INTERRUPT_1, 0, 0, &process_1_objects);

  std::vector<KernelObject> process_2_objects;
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_1_AND_2[1], CHANNEL_BETWEEN_1_AND_2[0],
               PROCESS_1, &process_2_objects);
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_2_AND_3[0], CHANNEL_BETWEEN_2_AND_3[1],
               PROCESS_3, &process_2_objects);
  CreateObject(ZX_OBJ_TYPE_JOB, JOB_2[0], JOB_2[1], 0, &process_2_objects);
  CreateObject(ZX_OBJ_TYPE_VMO, VMO_2, 0, 0, &process_2_objects);

  std::vector<KernelObject> process_3_objects;
  CreateObject(ZX_OBJ_TYPE_CHANNEL, CHANNEL_BETWEEN_2_AND_3[1], CHANNEL_BETWEEN_2_AND_3[0],
               PROCESS_2, &process_3_objects);
  CreateObject(ZX_OBJ_TYPE_SOCKET, SOCKET_BETWEEN_1_AND_3[1], SOCKET_BETWEEN_1_AND_3[0], PROCESS_1,
               &process_3_objects);
  CreateObject(ZX_OBJ_TYPE_FIFO, FIFO_3[0], FIFO_3[1], 0, &process_3_objects);
  CreateObject(ZX_OBJ_TYPE_VMAR, VMAR_3, 0, 0, &process_3_objects);

  std::vector<Process> processes;
  CreateProcess(PROCESS_1, "process1", process_1_objects, &processes);
  CreateProcess(PROCESS_2, "process2", process_2_objects, &processes);
  CreateProcess(PROCESS_3, "process3", process_3_objects, &processes);

  return processes;
}

TEST(Utils, PeerOwnerKoidFound) {
  std::vector<Process> initial_processes_data = InitialProcessesData();
  std::vector<Process> expected_processes_data = ExpectedProcessesData();

  FillPeerOwnerKoid(initial_processes_data);

  EXPECT_EQ(initial_processes_data.size(), expected_processes_data.size());
  size_t processes_count = initial_processes_data.size();
  for (size_t p = 0; p < processes_count; p++) {
    EXPECT_EQ(initial_processes_data[p].objects.size(), expected_processes_data[p].objects.size());
    size_t objects_count = initial_processes_data[p].objects.size();
    for (size_t o = 0; o < objects_count; o++) {
      ASSERT_EQ(initial_processes_data[p].objects[o].peer_owner_koid,
                expected_processes_data[p].objects[o].peer_owner_koid);
    }
  }
}

}  // namespace
}  // namespace process_explorer
