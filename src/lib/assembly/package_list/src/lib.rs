// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for interacting with a package list.

mod package_list;

// Classes used to generate package lists.
pub use package_list::PackageList;
pub use package_list::PackageUrlList;
pub use package_list::WritablePackageList;
