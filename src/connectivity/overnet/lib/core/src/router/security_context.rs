// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

/// Tracks the set of certificates that are needed to secure a Router
pub trait SecurityContext: Send + Sync + std::fmt::Debug {
    /// The node certificate
    fn node_cert(&self) -> &str;
    /// The node private key
    fn node_private_key(&self) -> &str;
    /// The root cert for this Overnet mesh
    fn root_cert(&self) -> &str;
}

pub(crate) async fn quiche_config_from_security_context(
    sec_ctx: &dyn SecurityContext,
) -> Result<quiche::Config, Error> {
    let node_cert = sec_ctx.node_cert().to_string();
    let node_private_key = sec_ctx.node_private_key().to_string();
    let root_cert = sec_ctx.root_cert().to_string();
    let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
    config.load_cert_chain_from_pem_file(&node_cert)?;
    config.load_priv_key_from_pem_file(&node_private_key)?;
    config.load_verify_locations_from_file(&root_cert)?;
    config.verify_peer(false);
    Ok(config)
}

/// SecurityContext implementation that just lists files to read for the various pieces.
#[derive(Debug)]
pub struct SimpleSecurityContext {
    /// The node certificate
    pub node_cert: &'static str,
    /// The node private key
    pub node_private_key: &'static str,
    /// The root cert for this Overnet mesh
    pub root_cert: &'static str,
}

impl SecurityContext for SimpleSecurityContext {
    fn node_cert(&self) -> &str {
        self.node_cert
    }

    fn node_private_key(&self) -> &str {
        self.node_private_key
    }

    fn root_cert(&self) -> &str {
        self.root_cert
    }
}

/// SecurityContext implementation that just lists files to read for the various pieces.
#[derive(Debug)]
pub struct StringSecurityContext {
    /// The node certificate
    pub node_cert: String,
    /// The node private key
    pub node_private_key: String,
    /// The root cert for this Overnet mesh
    pub root_cert: String,
}

impl SecurityContext for StringSecurityContext {
    fn node_cert(&self) -> &str {
        &self.node_cert
    }

    fn node_private_key(&self) -> &str {
        &self.node_private_key
    }

    fn root_cert(&self) -> &str {
        &self.root_cert
    }
}

/// SecurityContext builder that takes static data and turns it into temporary files
#[cfg(not(target_os = "fuchsia"))]
pub struct MemoryBuffers {
    /// The node certificate
    pub node_cert: &'static [u8],
    /// The node private key
    pub node_private_key: &'static [u8],
    /// The root cert for this Overnet mesh
    pub root_cert: &'static [u8],
}

#[cfg(not(target_os = "fuchsia"))]
impl MemoryBuffers {
    /// Turn these buffers into a SecurityContext
    pub fn into_security_context(self) -> Result<impl SecurityContext, Error> {
        use std::io::Write;

        #[derive(Debug)]
        struct SecCtx {
            node_cert: tempfile::NamedTempFile,
            node_private_key: tempfile::NamedTempFile,
            root_cert: tempfile::NamedTempFile,
        }

        impl SecurityContext for SecCtx {
            fn node_cert(&self) -> &str {
                self.node_cert.path().to_str().unwrap()
            }

            fn node_private_key(&self) -> &str {
                self.node_private_key.path().to_str().unwrap()
            }

            fn root_cert(&self) -> &str {
                self.root_cert.path().to_str().unwrap()
            }
        }

        let load = |contents| -> Result<_, Error> {
            let mut f = tempfile::NamedTempFile::new()?;
            f.write_all(contents)?;
            Ok(f)
        };

        Ok(SecCtx {
            node_cert: load(self.node_cert)?,
            node_private_key: load(self.node_private_key)?,
            root_cert: load(self.root_cert)?,
        })
    }
}
