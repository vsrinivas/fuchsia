use failure::{bail, ensure};
use std::collections::HashMap;
use wlan_rsn::Authenticator;

use crate::ap::aid::{self, AssociationId};
use crate::MacAddr;

#[derive(Debug)]
pub struct RemoteClient {
    pub addr: MacAddr,
    pub aid: AssociationId,
    // TODO: change this to Authenticator type when cl/202680 lands
    pub authenticator: Option<Authenticator>,
    _inner: (),
}

impl RemoteClient {
    fn new(addr: MacAddr, aid: AssociationId, authenticator: Option<Authenticator>) -> Self {
        RemoteClient {
            addr,
            aid,
            authenticator,
            _inner: (),
        }
    }
}

#[derive(Default)]
pub struct Map {
    clients: HashMap<MacAddr, RemoteClient>,
    aid_map: aid::Map,
}

impl Map {
    pub fn add_client(&mut self, addr: MacAddr, authenticator: Option<Authenticator>)
                      -> Result<AssociationId, failure::Error> {
        // just make this case an error for now; can tweak this behavior once we have a better
        // understanding of how this case can happen
        ensure!(self.get_client(&addr).is_none(), "client already exists in map");

        let aid = self.aid_map.assign_aid()?;
        let remote_client = RemoteClient::new(addr, aid, authenticator);
        self.clients.insert(addr, remote_client);
        Ok(aid)
    }

    pub fn get_client(&self, addr: &MacAddr) -> Option<&RemoteClient> {
        self.clients.get(addr)
    }

    pub fn get_mut_client(&mut self, addr: &MacAddr) -> Option<&mut RemoteClient> {
        self.clients.get_mut(addr)
    }

    // currently unused outside of tests, can remove attribute once used in actual code
    #[allow(dead_code)]
    pub fn remove_client(&mut self, addr: &MacAddr) -> Option<RemoteClient> {
        let remote_client = self.clients.remove(addr);
        remote_client.as_ref().map(|rc| self.aid_map.release_aid(rc.aid));
        remote_client
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_remote_client_map() {
        let mut client_map: Map = Default::default();
        let client_addr1 = addr(1);
        let client_addr2 = addr(2);
        let client_addr3 = addr(3);
        assert_eq!(add_client(&mut client_map, client_addr1).unwrap(), 1);
        assert_eq!(client_map.get_client(&client_addr1).unwrap().aid, 1);
        assert_eq!(add_client(&mut client_map, client_addr2).unwrap(), 2);
        client_map.remove_client(&client_addr1);
        assert_eq!(add_client(&mut client_map, client_addr3).unwrap(), 1);
    }

    #[test]
    fn test_add_client_multiple_times() {
        let mut client_map: Map = Default::default();
        assert!(add_client(&mut client_map, addr(1)).is_ok());
        let result = add_client(&mut client_map, addr(1));
        assert!(result.is_err());
        assert_eq!(format!("{}", result.unwrap_err()), "client already exists in map");
    }

    fn add_client(client_map: &mut Map, addr: MacAddr)
                  -> Result<AssociationId, failure::Error> {
        client_map.add_client(addr, None)
    }

    fn addr(id: u32) -> MacAddr {
        // impl doesn't matter, just need a unique address for each id for our test
        use std::mem;
        let mac_addr: [u8; 4] = unsafe { mem::transmute(id) };
        [mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], 0, 0]
    }
}