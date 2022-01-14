// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_STARTUP_ANNOTATIONS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_STARTUP_ANNOTATIONS_H_

#include <map>
#include <string>

#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback {

// Gets annotations that are available immediately and synchronously when the component starts and
// never change while it is running.
Annotations GetStartupAnnotations(const RebootLog& reboot_log);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_STARTUP_ANNOTATIONS_H_
