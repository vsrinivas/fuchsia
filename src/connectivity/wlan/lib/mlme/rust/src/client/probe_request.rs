use {
    crate::error::Error, anyhow::format_err, banjo_fuchsia_hardware_wlan_mac as banjo_wlan_mac,
    banjo_fuchsia_hardware_wlanphyinfo as banjo_hw_wlaninfo,
    banjo_fuchsia_wlan_ieee80211 as banjo_ieee80211,
    wlan_frame_writer::write_frame_with_dynamic_buf,
};

pub struct ProbeRequestSeries {
    ssids_list: Vec<banjo_ieee80211::CSsid>,
    mac_header: Vec<u8>,
    ies_series: Vec<ProbeRequestIes>,
}

struct ProbeRequestIes {
    channels: Vec<u8>,
    ies: Vec<u8>,
}

pub struct ProbeRequestParams<'a> {
    pub ssids_list: &'a Vec<banjo_ieee80211::CSsid>,
    pub mac_header: &'a Vec<u8>,
    pub channels: Vec<u8>,
    pub ies: Vec<u8>,
}

impl ProbeRequestSeries {
    pub fn new(
        ssids_list: Vec<banjo_ieee80211::CSsid>,
        mac_header: Vec<u8>,
        wlanmac_info: &banjo_wlan_mac::WlanmacInfo,
        channel_list: Vec<u8>,
    ) -> Result<ProbeRequestSeries, Error> {
        Ok(ProbeRequestSeries {
            ssids_list,
            mac_header,
            ies_series: Self::ies_series(wlanmac_info, channel_list)?,
        })
    }

    pub fn next<'a>(&'a mut self) -> Option<ProbeRequestParams<'a>> {
        let probe_request_ies = self.ies_series.pop()?;
        Some(ProbeRequestParams {
            ssids_list: &self.ssids_list,
            mac_header: &self.mac_header,
            channels: probe_request_ies.channels,
            ies: probe_request_ies.ies,
        })
    }

    fn ies_series(
        wlanmac_info: &banjo_wlan_mac::WlanmacInfo,
        channel_list: Vec<u8>,
    ) -> Result<Vec<ProbeRequestIes>, Error> {
        let (two_ghz_channels, five_ghz_channels) =
            channel_list.into_iter().fold((vec![], vec![]), |mut acc, c| {
                match band_from_channel_number(c) {
                    Band::TwoGhz => acc.0.push(c),
                    Band::FiveGhz => acc.1.push(c),
                }
                (acc.0, acc.1)
            });

        Ok(vec![
            ProbeRequestIes {
                channels: two_ghz_channels,
                ies: probe_request_ies_for_band(
                    &wlanmac_info,
                    banjo_hw_wlaninfo::WlanInfoBand::TWO_GHZ,
                )?,
            },
            ProbeRequestIes {
                channels: five_ghz_channels,
                ies: probe_request_ies_for_band(
                    &wlanmac_info,
                    banjo_hw_wlaninfo::WlanInfoBand::FIVE_GHZ,
                )?,
            },
        ])
    }
}

fn probe_request_ies_for_band(
    wlanmac_info: &banjo_wlan_mac::WlanmacInfo,
    band: banjo_hw_wlaninfo::WlanInfoBand,
) -> Result<Vec<u8>, Error> {
    let rates = probe_request_rates_for_band(wlanmac_info, band)?;
    Ok(write_frame_with_dynamic_buf!(vec![], {
        ies: {
            supported_rates: rates,
            extended_supported_rates: {/* continue rates */},
        }
    })?
    .0)
}

fn probe_request_rates_for_band(
    wlanmac_info: &banjo_wlan_mac::WlanmacInfo,
    band: banjo_hw_wlaninfo::WlanInfoBand,
) -> Result<Vec<u8>, Error> {
    let band_info = band_info_for_band(&wlanmac_info, band)
        .ok_or(format_err!("no band found for band {:?}", band))?;
    Ok(band_info.rates.iter().cloned().filter(|r| *r > 0).collect())
}

pub fn probe_request_rates_for_channel(
    wlanmac_info: &banjo_wlan_mac::WlanmacInfo,
    channel_number: u8,
) -> Result<Vec<u8>, Error> {
    let band_info = band_info_for_channel_number(&wlanmac_info, channel_number)
        .ok_or(format_err!("no band info for channel {:?}", channel_number))?;
    Ok(band_info.rates.iter().cloned().filter(|r| *r > 0).collect())
}

// Private enum to simplify matching band values defined by banjo_hw_wlaninfo::WlanInfoBand
enum Band {
    TwoGhz,
    FiveGhz,
}

impl From<Band> for banjo_hw_wlaninfo::WlanInfoBand {
    fn from(band: Band) -> banjo_hw_wlaninfo::WlanInfoBand {
        match band {
            Band::TwoGhz => banjo_hw_wlaninfo::WlanInfoBand::TWO_GHZ,
            Band::FiveGhz => banjo_hw_wlaninfo::WlanInfoBand::FIVE_GHZ,
        }
    }
}

fn band_from_channel_number(channel: u8) -> Band {
    // highest 2.4 GHz band channel is 14
    if channel > 14 {
        Band::FiveGhz
    } else {
        Band::TwoGhz
    }
}

fn band_info_for_channel_number(
    wlanmac_info: &banjo_wlan_mac::WlanmacInfo,
    channel: u8,
) -> Option<&banjo_hw_wlaninfo::WlanInfoBandInfo> {
    band_info_for_band(wlanmac_info, band_from_channel_number(channel).into())
}

fn band_info_for_band(
    wlanmac_info: &banjo_wlan_mac::WlanmacInfo,
    band: banjo_hw_wlaninfo::WlanInfoBand,
) -> Option<&banjo_hw_wlaninfo::WlanInfoBandInfo> {
    wlanmac_info.bands[..wlanmac_info.bands_count as usize].iter().filter(|b| b.band == band).next()
}
