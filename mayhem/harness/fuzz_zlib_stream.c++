// Fuzzes workerd's node:zlib handle (src/workerd/api/node/zlib-util.c++, src/workerd/api/compression.c++):
// CompressionStream<...>::write<false>, the writeSync() a Worker reaches through the internal zlib binding,
// on zlib, Brotli and zstd streams.
//
// Input layout: [mode][params x4][nWrites][bufSize hi/lo] then per write
//   [flush][sameBuffer][inOff x2][inLen x2][outOff x2][outLen x2]
// and the rest fills the shared buffer. A Worker owns both ArrayBuffers it passes, and may pass the same
// one twice, so `sameBuffer` makes the input and output views alias one buffer as it could.
//
// V8 is replaced by v8_stubs.c++: a fake ArrayBuffer is a heap block of exactly its size, so ASan sees any
// access the compression library makes past the view.
#include "../zlib_unity.h"

namespace {

using namespace workerd;
using namespace workerd::api;
using namespace workerd::api::node;

class Reader {
 public:
  Reader(const uint8_t* data, size_t size): data(data), size(size) {}
  uint8_t u8() { return pos < size ? data[pos++] : 0; }
  uint16_t u16() {
    uint16_t hi = u8();
    return static_cast<uint16_t>((hi << 8) | u8());
  }
  const uint8_t* rest() const { return data + pos; }
  size_t restSize() const { return size - pos; }

 private:
  const uint8_t* data;
  size_t size;
  size_t pos = 0;
};

struct Op {
  uint8_t flushSel;
  bool sameBuffer;
  uint16_t inOff, inLen, outOff, outLen;
};

// Node's zlib module validates `flush` in JS before write(): Z_* for zlib, BROTLI_OPERATION_* (0..3) for
// Brotli and ZSTD_e_* (0..2) for zstd; the handle casts the value straight to the codec's enum.
constexpr int kZlibFlushes[] = {Z_NO_FLUSH, Z_PARTIAL_FLUSH, Z_SYNC_FLUSH, Z_FULL_FLUSH, Z_FINISH, Z_BLOCK};
constexpr int kBrotliFlushes[] = {0, 1, 2, 3};
constexpr int kZstdFlushes[] = {0, 1, 2};

template <typename Stream>
void drive(Stream& stream, const kj::Array<Op>& ops, kj::ArrayPtr<const int> flushes,
    mayhem::FakeBuffer& shared, mayhem::FakeBuffer& other) {
  stream.initialized = true;
  for (auto& op: ops) {
    mayhem::FakeBuffer* in = &shared;
    mayhem::FakeBuffer* out = op.sameBuffer ? &shared : &other;
    kj::runCatchingExceptions([&]() {
      stream.template write<false>(fakeLock(), flushes[op.flushSel % flushes.size()], wrap(in), op.inOff, op.inLen, wrap(out),
          op.outOff, op.outLen);
    });
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 8) return -1;
  Reader r(data, size);
  uint8_t mode = r.u8() % 11;
  uint8_t p0 = r.u8(), p1 = r.u8(), p2 = r.u8(), p3 = r.u8();
  size_t nOps = 1 + r.u8() % 6;
  size_t bufSize = 64 + r.u16() % 4096;

  kj::Vector<Op> opsBuilder;
  for (size_t i = 0; i < nOps; i++) {
    Op op;
    op.flushSel = r.u8();
    op.sameBuffer = (r.u8() & 3) != 0;
    op.inOff = r.u16() % bufSize;
    op.inLen = r.u16() % (bufSize + 1);
    op.outOff = r.u16() % bufSize;
    op.outLen = r.u16() % (bufSize + 1);
    opsBuilder.add(op);
  }
  auto ops = opsBuilder.releaseAsArray();

  // Heap blocks of exactly bufSize, so ASan flags any access outside them.
  auto sharedData = kj::heapArray<uint8_t>(bufSize);
  auto otherData = kj::heapArray<uint8_t>(bufSize);
  memset(sharedData.begin(), 0, bufSize);
  memset(otherData.begin(), 0, bufSize);
  size_t n = r.restSize() < bufSize ? r.restSize() : bufSize;
  memcpy(sharedData.begin(), r.rest(), n);
  mayhem::FakeBuffer shared{sharedData.begin(), bufSize};
  mayhem::FakeBuffer other{otherData.begin(), bufSize};

  auto result = kj::runCatchingExceptions([&]() {
    switch (mode) {
      case 0 ... 6: {
        static constexpr ZlibMode zmodes[] = {ZlibMode::DEFLATE, ZlibMode::INFLATE, ZlibMode::GZIP,
          ZlibMode::GUNZIP, ZlibMode::DEFLATERAW, ZlibMode::INFLATERAW, ZlibMode::UNZIP};
        ZlibUtil::ZlibStream s(zmodes[mode], memoryTarget());
        int windowBits = 8 + p0 % 8;  // 8..15; initialize() adds the gzip/raw/auto-detect offsets itself
        s.context()->initialize(static_cast<int>(p1 % 11) - 1, windowBits, 1 + p2 % 9, p3 % 5, kj::none);
        drive(s, ops, kZlibFlushes, shared, other);
        break;
      }
      case 7: {
        ZlibUtil::BrotliCompressionStream<BrotliEncoderContext> s(ZlibMode::BROTLI_ENCODE, memoryTarget());
        s.context()->setParams(BROTLI_PARAM_QUALITY, p0 % 12);
        s.context()->setParams(BROTLI_PARAM_LGWIN, 10 + p1 % 15);
        s.context()->setParams(BROTLI_PARAM_MODE, p2 % 3);
        drive(s, ops, kBrotliFlushes, shared, other);
        break;
      }
      case 8: {
        ZlibUtil::BrotliCompressionStream<BrotliDecoderContext> s(ZlibMode::BROTLI_DECODE, memoryTarget());
        drive(s, ops, kBrotliFlushes, shared, other);
        break;
      }
      case 9: {
        ZlibUtil::ZstdCompressionStream<ZstdEncoderContext> s(ZlibMode::ZSTD_ENCODE, memoryTarget());
        s.context()->initialize(ZSTD_CONTENTSIZE_UNKNOWN);
        s.context()->setParams(ZSTD_c_compressionLevel, static_cast<int>(p0 % 23) - 3);
        drive(s, ops, kZstdFlushes, shared, other);
        break;
      }
      default: {
        ZlibUtil::ZstdCompressionStream<ZstdDecoderContext> s(ZlibMode::ZSTD_DECODE, memoryTarget());
        s.context()->initialize();
        drive(s, ops, kZstdFlushes, shared, other);
        break;
      }
    }
  });
  (void)result;
  return 0;
}
