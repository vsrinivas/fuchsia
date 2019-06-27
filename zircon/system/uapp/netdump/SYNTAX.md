# Syntax
The packet filter language syntax is as follows.
Keywords are in **bold**.
Optional terms are in `[square brackets]`.
Placeholders for literals are in `<angle brackets>`.
Binary logical operators associate to the left.
All keywords and port aliases should be in lower case.
<pre><code>
       expr ::= <b>(</b> expr <b>)</b>
              | <b>not</b> expr  | expr <b>and</b> expr | expr <b>or</b> expr
              | eth_expr  | host_expr     | trans_expr
length_expr ::= <b>greater</b> \<len> | <b>less</b> \<len>
       type ::= <b>src</b> | <b>dst</b>
   eth_expr ::= length_expr
              | <b>ether</b> [type] <b>host</b> \<mac_addr>
              | [<b>ether</b> <b>proto</b>] net_expr
   net_expr ::= <b>arp</b>
              | <b>vlan</b>
              | <b>ip</b>  [length_expr | host_expr | trans_expr]
              | <b>ip6</b> [length_expr | host_expr | trans_expr]
  host_expr ::= [type] <b>host</b> \<ip_addr>
 trans_expr ::= [<b>proto</b>] <b>icmp</b>
              | [<b>proto</b>] <b>tcp</b> [port_expr]
              | [<b>proto</b>] <b>udp</b> [port_expr]
              | port_expr
  port_expr ::= [type] <b>port</b> \<port_lst>
</code></pre>

*   `<len>`: Packet length in bytes. Greater or less comparison is inclusive of `len`.
*   `<mac_addr>`: MAC address, e.g. `DE:AD:BE:EF:D0:0D`. Hex digits are case-insensitive.
*   `<ip_addr>`: IP address consistent with the IP version specified previously.
    E.g. `192.168.1.10`, `2001:4860:4860::8888`.
*   `<port_lst>`: List of ports or port ranges separated by commas, e.g. `13,ssh,6000-7000,20`.
    The following aliases for defined ports and port ranges can be used as an item in the list, but
    not as part of a range (`3,dhcp,12` is allowed, `http-100` is not):

  |Alias    | Port(s)                   |   
  |:--------| :-------------------------|   
  |`dhcp`   | `67-68`                   |   
  |`dns`    | `53`                      |
  |`echo`   | `7`                       |
  |`ftpxfer`| `20`                      |
  |`ftpctl` | `21`                      |
  |`http`   | `80`                      |
  |`https`  | `443`                     | 
  |`irc`    | `194`                     | 
  |`ntp`    | `123`                     | 
  |`sftp`   | `115`                     |
  |`ssh`    | `22`                      |
  |`telnet` | `23`                      |
  |`tftp`   | `69`                      |
  |`dbglog` | Netboot debug log port    |
  |`dbgack` | Netboot debug log ack port|

# Synonyms
The following aliases may be used instead of the keywords listed in the syntax:

Keyword | Alias
:-------| :----------
`ip`    | `ip4`
`port`  | `portrange`
