// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONSTANTS_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONSTANTS_H_

namespace fuchsia {
namespace feedback {

constexpr char kAnnotationBuildBoard[] = "build.board";
constexpr char kAnnotationBuildLatestCommitDate[] = "build.latest-commit-date";
constexpr char kAnnotationBuildProduct[] = "build.product";
constexpr char kAnnotationBuildVersion[] = "build.version";
constexpr char kAnnotationChannel[] = "channel";
constexpr char kAnnotationDeviceBoardName[] = "device.board-name";

constexpr char kAttachmentAnnotations[] = "annotations.json";
constexpr char kAttachmentBuildSnapshot[] = "build.snapshot.xml";
constexpr char kAttachmentInspect[] = "inspect.json";
constexpr char kAttachmentLogKernel[] = "log.kernel.txt";
constexpr char kAttachmentLogSystem[] = "log.system.txt";

constexpr char kAttachmentBundle[] = "fuchsia_feedback_data.zip";

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONSTANTS_H_
