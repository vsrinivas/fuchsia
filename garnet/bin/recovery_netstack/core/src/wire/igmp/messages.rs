// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

create_net_enum! {
    MessageType,
    MembershipQuery: MEMBERSHIP_QUERY = 0x11,
    MembershipReportV1: MEMBERSHIP_REPORT_V1 = 0x12,
    MembershipReportV2: MEMBERSHIP_REPORT_V2 = 0x16,
    MembershipReportV3: MEMBERSHIP_REPORT_V3 = 0x22,
    LeaveVroup: LEAVE_GROUP = 0x17,
}
