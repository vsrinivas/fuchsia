// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_PROCESS_EXPLORER_UTILS_H_
#define SRC_DEVELOPER_PROCESS_EXPLORER_UTILS_H_

#include <lib/zx/process.h>
#include <zircon/types.h>

#include <string>
#include <vector>

namespace process_explorer {

// An object can be a: channel, event, socket, etc.
struct KernelObject {
  zx_obj_type_t object_type;
  zx_koid_t koid;
  zx_koid_t related_koid;
  zx_koid_t peer_owner_koid;
};

// The koid and name of a process and it's objects.
struct Process {
  zx_koid_t koid;
  std::string name;
  std::vector<KernelObject> objects;
};

/* Returns the process information vector as a JSON string. In this format:
    {
        "Processes":[
            {
                "koid":1097,
                "name":"bin/component_manager",
                "objects":[
                    {
                        "type":17,
                        "koid":41903,
                        "related_koid":1033,
                        "peer_owner_koid":0
                    },
                    ...
                ]
            },
            ...
        ]
    }
*/
std::string WriteProcessesDataAsJson(std::vector<Process> processes_data);

// Returns an array of zx_info_handle_extended_t one for each handle in the Process at the moment of
// the call.
zx_status_t GetHandles(zx::unowned_process process, std::vector<zx_info_handle_extended_t>* out);

// Finds the peer_owner_koid field for objects that have two ends (such as channels or sockets).
// The function is only able to find the peer_owner_koid when each end of the object is
// owned by a process at the time the processes are walked and their objects are retrieved.
void FillPeerOwnerKoid(std::vector<Process>& processes_data);

}  // namespace process_explorer

#endif  // SRC_DEVELOPER_PROCESS_EXPLORER_UTILS_H_
