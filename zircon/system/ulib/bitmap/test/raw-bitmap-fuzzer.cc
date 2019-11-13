#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fuzzer/FuzzedDataProvider.h>

enum BitmapOps { Set, ClearAll, Scan, Find, Get, Reset, kMaxValue = Reset };

static const size_t kMaxBitmapSize = 10 * 1024 * 1024;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  bitmap::RawBitmapGeneric<bitmap::DefaultStorage> object;
  FuzzedDataProvider fuzzed_data(data, size);
  while (fuzzed_data.remaining_bytes() > 0) {
    auto op = fuzzed_data.ConsumeEnum<BitmapOps>();
    switch (op) {
      case Set: {
        auto index = fuzzed_data.ConsumeIntegral<size_t>();
        auto next = fuzzed_data.ConsumeIntegral<size_t>();
        object.Set(index, next);
        break;
      }
      case ClearAll: {
        object.ClearAll();
        break;
      }
      case Scan: {
        auto off = fuzzed_data.ConsumeIntegral<size_t>();
        auto max = fuzzed_data.ConsumeIntegral<size_t>();
        auto set = fuzzed_data.ConsumeBool();

        size_t out;
        object.Scan(off, max, set, &out);
        break;
      }
      case Find: {
        auto set = fuzzed_data.ConsumeBool();
        auto off = fuzzed_data.ConsumeIntegral<size_t>();
        auto max = fuzzed_data.ConsumeIntegral<size_t>();
        auto runLen = fuzzed_data.ConsumeIntegral<size_t>();

        size_t findOut;
        object.Find(set, off, max, runLen, &findOut);
        break;
      }
      case Get: {
        auto bit = fuzzed_data.ConsumeIntegral<size_t>();
        auto lastBit = fuzzed_data.ConsumeIntegral<size_t>();

        size_t first;
        object.Get(bit, lastBit, &first);
        break;
      }
      case Reset: {
        auto memory = fuzzed_data.ConsumeIntegralInRange<size_t>(0, kMaxBitmapSize);
        object.Reset(memory);
        break;
      }
    }
  }
  return 0;
}
