nand-broker allows for serialized entry to the raw nand, which ensures only a single client is ever
talking to the nand device, and all higher level clients have disconnected. In addition, access to
raw nand is restricted from the system during production where nand-broker is not present.
