use serde::Deserialize;

#[derive(Deserialize, Debug)]
#[serde(untagged)]
pub enum LightSensorConfig {
    VendorAndProduct { vendor_id: u32, product_id: u32 },
}
