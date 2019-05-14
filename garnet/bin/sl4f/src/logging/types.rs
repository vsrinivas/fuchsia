// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub enum LoggingMethod {
    LogErr,
    LogInfo,
    LogWarn,
    LoggingMethodUndefined,
}

impl LoggingMethod {
    pub fn from_str(method: &String) -> LoggingMethod {
        match method.as_ref() {
            "LogErr" => LoggingMethod::LogErr,
            "LogInfo" => LoggingMethod::LogInfo,
            "LogWarn" => LoggingMethod::LogWarn,
            _ => LoggingMethod::LoggingMethodUndefined,
        }
    }
}
