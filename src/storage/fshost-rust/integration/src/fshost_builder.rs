// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_fshost as ffshost, fidl_fuchsia_io as fio, fidl_fuchsia_logger as flogger,
    fidl_fuchsia_process as fprocess,
    fuchsia_component_test::{Capability, ChildOptions, ChildRef, RealmBuilder, Ref, Route},
};

/// Builder for the fshost component. This handles configuring the fshost component to use and
/// structured config overrides to set, as well as setting up the expected protocols to be routed
/// between the realm builder root and the fshost child when the test realm is built.
///
/// Any desired additional config overrides should be added to this builder. New routes for exposed
/// capabilities from the fshost component or offered capabilities to the fshost component should
/// be added to the [`FshostBuilder::build`] function below.
#[derive(Debug, Clone)]
pub struct FshostBuilder {
    component_name: &'static str,
    no_zxcrypt: bool,
    fvm_ramdisk: bool,
    ramdisk_prefix: Option<&'static str>,
    blobfs_max_bytes: Option<u64>,
    data_max_bytes: Option<u64>,
}

impl FshostBuilder {
    pub fn new(component_name: &'static str) -> FshostBuilder {
        FshostBuilder {
            component_name,
            no_zxcrypt: false,
            fvm_ramdisk: false,
            ramdisk_prefix: None,
            blobfs_max_bytes: None,
            data_max_bytes: None,
        }
    }

    pub fn set_no_zxcrypt(&mut self) -> &mut Self {
        self.no_zxcrypt = true;
        self
    }

    pub fn set_fvm_ramdisk(&mut self) -> &mut Self {
        self.fvm_ramdisk = true;
        self
    }

    pub fn set_ramdisk_prefix(&mut self, prefix: &'static str) -> &mut Self {
        self.ramdisk_prefix = Some(prefix);
        self
    }

    pub fn set_blobfs_max_bytes(&mut self, bytes: u64) -> &mut Self {
        self.blobfs_max_bytes = Some(bytes);
        self
    }

    pub fn set_data_max_bytes(&mut self, bytes: u64) -> &mut Self {
        self.data_max_bytes = Some(bytes);
        self
    }

    pub(crate) async fn build(&self, realm_builder: &RealmBuilder) -> ChildRef {
        let fshost_url = format!("#meta/{}.cm", self.component_name);
        println!("using {} as test-fshost", fshost_url);
        let fshost = realm_builder
            .add_child("test-fshost", fshost_url, ChildOptions::new().eager())
            .await
            .unwrap();

        realm_builder.init_mutable_config_from_package(&fshost).await.unwrap();

        // fshost config overrides
        if self.no_zxcrypt {
            realm_builder
                .set_config_value_bool(&fshost, "no_zxcrypt", self.no_zxcrypt)
                .await
                .unwrap();
        }
        if self.fvm_ramdisk {
            realm_builder
                .set_config_value_bool(&fshost, "fvm_ramdisk", self.fvm_ramdisk)
                .await
                .unwrap();
        }
        if let Some(prefix) = self.ramdisk_prefix {
            realm_builder.set_config_value_string(&fshost, "ramdisk_prefix", prefix).await.unwrap();
        }
        if let Some(blobfs_max_bytes) = self.blobfs_max_bytes {
            realm_builder
                .set_config_value_uint64(&fshost, "blobfs_max_bytes", blobfs_max_bytes)
                .await
                .unwrap();
        }
        if let Some(data_max_bytes) = self.data_max_bytes {
            realm_builder
                .set_config_value_uint64(&fshost, "data_max_bytes", data_max_bytes)
                .await
                .unwrap();
        }

        realm_builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<ffshost::AdminMarker>())
                    .capability(Capability::directory("blob").rights(fio::RW_STAR_DIR))
                    .capability(Capability::directory("data").rights(fio::RW_STAR_DIR))
                    .capability(Capability::directory("tmp").rights(fio::RW_STAR_DIR))
                    .from(&fshost)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        realm_builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<flogger::LogSinkMarker>())
                    .capability(Capability::protocol::<fprocess::LauncherMarker>())
                    .from(Ref::parent())
                    .to(&fshost),
            )
            .await
            .unwrap();

        fshost
    }
}
