// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]
#![warn(clippy::all)]

mod args;
mod package_build;
mod repo_create;
mod repo_publish;

pub use crate::{
    args::{PackageBuildCommand, RepoCreateCommand, RepoPublishCommand},
    package_build::cmd_package_build,
    repo_create::cmd_repo_create,
    repo_publish::cmd_repo_publish,
};
