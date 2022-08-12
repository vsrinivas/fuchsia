// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_hash::Hash;
use serde::Serialize;
use std::collections::HashSet;
use std::fmt;

#[derive(Debug, Serialize, Eq, PartialEq, Default)]
pub struct PackageSizeInfo {
    pub name: String,
    /// Space used by this package in blobfs if each blob is counted fully.
    pub used_space_in_blobfs: u64,
    /// Size of the package in blobfs if each blob is divided equally among all the packages that reference it.
    pub proportional_size: u64,
    /// Blobs in this package and information about their size.
    pub blobs: Vec<PackageBlobSizeInfo>,
}

#[derive(Debug, Serialize, Eq, PartialEq)]
pub struct PackageBlobSizeInfo {
    pub merkle: Hash,
    pub path_in_package: String,
    /// Space used by this blob in blobfs
    pub used_space_in_blobfs: u64,
    /// Number of occurrences of the blob across all packages.
    pub share_count: u64,
}

pub struct PackageSizeInfos(pub Vec<PackageSizeInfo>);

/// Implementing Display to show a easy to comprehend stuctured output of PackageSizeInfos
impl fmt::Display for PackageSizeInfos {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut duplicate_found = false;
        let package_column_width: usize = 50;
        let path_column_width: usize = 50;
        let size_column_width: usize = 10;
        let proportional_size_column_width: usize = 20;
        let share_column_width: usize = 5;
        writeln!(
            f,
            "{0: <4$}  {1: >5$}  {2: >6$}  {3: >7$}",
            "Package",
            "Size",
            "Proportional Size",
            "Share",
            package_column_width,
            size_column_width,
            proportional_size_column_width,
            share_column_width
        )
        .unwrap();
        self.0.iter().for_each(|p| {
            writeln!(
                f,
                "{0: <4$}  {1: >5$}  {2: >6$}  {3: >7$}",
                p.name,
                p.used_space_in_blobfs,
                p.proportional_size,
                "",
                package_column_width,
                size_column_width,
                proportional_size_column_width,
                share_column_width
            )
            .unwrap();
            let mut merkle_set: HashSet<String> = HashSet::new();
            p.blobs.iter().for_each(|b| {
                writeln!(
                    f,
                    "{0: <4$}  {1: >5$}  {2: >6$}  {3: >7$}",
                    process_path_str(
                        b.path_in_package.to_string(),
                        path_column_width,
                        merkle_set.contains(&b.merkle.to_string())
                    ),
                    b.used_space_in_blobfs,
                    b.used_space_in_blobfs / b.share_count,
                    b.share_count,
                    path_column_width,
                    size_column_width,
                    proportional_size_column_width,
                    share_column_width
                )
                .unwrap();
                duplicate_found |= merkle_set.contains(&b.merkle.to_string());
                merkle_set.insert(b.merkle.to_string());
            });
        });
        if duplicate_found {
            writeln!(f,"\n* indicates that this blob is a duplicate within this package and it therefore does not contribute to the overall package size").unwrap();
        }

        Ok(())
    }
}

fn process_path_str(input_path: String, column_width: usize, is_duplicate: bool) -> String {
    let path_str = if is_duplicate { format!("{}*", input_path) } else { input_path };
    if path_str.len() > (column_width - 3) {
        let mut v: Vec<String> =
            textwrap::wrap(&path_str, column_width - 5).iter().map(|x| x.to_string()).collect();
        let first_line = format!("   {0: <1$}", &v[0], column_width - 3);
        let last_line = format!("{0: <1$}", &v[v.len() - 1], column_width - 5);
        let len = v.len();
        v[0] = first_line;
        v[len - 1] = last_line;
        v.join("\n     ")
    } else {
        format!("   {0: <1$}", path_str, column_width - 3)
    }
}
