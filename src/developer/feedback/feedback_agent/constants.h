// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONSTANTS_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONSTANTS_H_

namespace feedback {

constexpr char kAnnotationBuildBoard[] = "build.board";
constexpr char kAnnotationBuildIsDebug[] = "build.is_debug";
constexpr char kAnnotationBuildLatestCommitDate[] = "build.latest-commit-date";
constexpr char kAnnotationBuildProduct[] = "build.product";
constexpr char kAnnotationBuildVersion[] = "build.version";
constexpr char kAnnotationChannel[] = "channel";
constexpr char kAnnotationDeviceBoardName[] = "device.board-name";
constexpr char kAnnotationDeviceUptime[] = "device.uptime";
constexpr char kAnnotationDeviceUTCTime[] = "device.utc-time";
constexpr char kAnnotationHardwareProductLanguage[] = "hardware.product.language";
constexpr char kAnnotationHardwareProductLocaleList[] = "hardware.product.locale-list";
constexpr char kAnnotationHardwareProductManufacturer[] = "hardware.product.manufacturer";
constexpr char kAnnotationHardwareProductModel[] = "hardware.product.model";
constexpr char kAnnotationHardwareProductName[] = "hardware.product.name";
constexpr char kAnnotationHardwareProductRegulatoryDomain[] = "hardware.product.regulatory-domain";
constexpr char kAnnotationHardwareProductSKU[] = "hardware.product.sku";

constexpr char kAttachmentAnnotations[] = "annotations.json";
constexpr char kAttachmentBuildSnapshot[] = "build.snapshot.xml";
constexpr char kAttachmentInspect[] = "inspect.json";
constexpr char kAttachmentLogKernel[] = "log.kernel.txt";
constexpr char kAttachmentLogSystem[] = "log.system.txt";

constexpr char kAttachmentBundle[] = "fuchsia_feedback_data.zip";

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONSTANTS_H_
