// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/111251): Implement for production System API.

#[cfg(test)]
pub mod fake {
    use crate::api::DataSource as DataSourceApi;
    use crate::api::DataSourceKind;
    use crate::api::DataSourceVersion;
    use std::iter;

    #[derive(Clone, Debug, Default, Eq, PartialEq)]
    pub(crate) struct DataSource;

    impl DataSourceApi for DataSource {
        type SourcePath = &'static str;

        fn kind(&self) -> DataSourceKind {
            DataSourceKind::Unknown
        }

        fn parent(&self) -> Option<Box<dyn DataSourceApi<SourcePath = Self::SourcePath>>> {
            None
        }

        fn children(
            &self,
        ) -> Box<dyn Iterator<Item = Box<dyn DataSourceApi<SourcePath = Self::SourcePath>>>>
        {
            Box::new(iter::empty())
        }

        fn path(&self) -> Option<Self::SourcePath> {
            None
        }

        fn version(&self) -> DataSourceVersion {
            DataSourceVersion::Unknown
        }
    }
}
