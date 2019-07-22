class FakeI2c : ddk::I2cProtocol<FakeI2c> {
 public:
  void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                   void* cookie) {
    callback(cookie, ZX_ERR_NOT_SUPPORTED, nullptr, 0);
  }

  zx_status_t I2cGetMaxTransferSize(size_t* out_size) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }
};
