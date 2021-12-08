use banjo_fuchsia_wlan_ieee80211 as banjo_ieee80211;

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
    fn next<'a>(&'a mut self) -> Option<ProbeRequestParams<'a>> {
        let probe_request_ies = self.ies_series.pop()?;
        Some(ProbeRequestParams {
            ssids_list: &self.ssids_list,
            mac_header: &self.mac_header,
            channels: probe_request_ies.channels,
            ies: probe_request_ies.ies,
        })
    }
}
