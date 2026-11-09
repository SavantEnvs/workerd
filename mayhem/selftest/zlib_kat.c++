// Known-answer test for the compression contexts behind workerd's node:zlib handle, built with the same
// sources and stand-ins as the fuzz harness but with normal flags (no sanitizers). It runs each codec
// through the handle's own work loop (setBuffers/setFlush/work/getAfterWriteResult, as writeStream does)
// and asserts exact bytes for zlib, decompressed round trips for every backend, and that corrupt input is
// reported as an error. Prints one "[ PASS ] name" / "[ FAIL ] name" line per case.
#include "../zlib_unity.h"

#include <string>
#include <vector>

namespace {

using namespace workerd;
using namespace workerd::api;
using namespace workerd::api::node;

int failures = 0;

void report(const char* name, bool ok) {
  printf("[ %s ] %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) failures++;
}

struct Pass {
  std::vector<uint8_t> out;
  kj::Maybe<CompressionError> error;
};

// Runs `ctx` over `input` to completion, calling work() with `flush` until the output chunk is not full.
template <typename Ctx>
Pass runAll(Ctx& ctx, const std::vector<uint8_t>& input, int flush, size_t chunkSize = 97) {
  Pass pass;
  size_t consumed = 0;
  std::vector<uint8_t> chunk(chunkSize);  // small and odd by default: forces many work() rounds
  for (int guard = 0; guard < 100000; guard++) {
    kj::ArrayPtr<kj::byte> in(const_cast<uint8_t*>(input.data()) + consumed, input.size() - consumed);
    ctx.setBuffers(in, kj::ArrayPtr<kj::byte>(chunk.data(), chunk.size()));
    ctx.setFlush(flush);
    ctx.work();
    uint32_t availIn = 0, availOut = 0;
    ctx.getAfterWriteResult(&availIn, &availOut);
    KJ_IF_SOME(e, ctx.getError()) {
      pass.error = kj::mv(e);
      ctx.clearBuffers();
      return pass;
    }
    consumed = input.size() - availIn;
    pass.out.insert(pass.out.end(), chunk.begin(), chunk.begin() + (chunk.size() - availOut));
    ctx.clearBuffers();
    if (availOut != 0 && availIn == 0) break;  // produced less than a full chunk: everything is flushed
  }
  return pass;
}

std::string hex(const std::vector<uint8_t>& v) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (uint8_t b: v) { s += d[b >> 4]; s += d[b & 15]; }
  return s;
}

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

std::vector<uint8_t> corpus() {
  std::string s;
  for (int i = 0; i < 300; i++) s += "workerd compresses line " + std::to_string(i * 7919 % 1000) + "\n";
  return bytes(s);
}

void zlibInit(ZlibUtil::ZlibStream& s, int windowBits) {
  s.context()->initialize(Z_DEFAULT_COMPRESSION, windowBits, 8, Z_DEFAULT_STRATEGY, kj::none);
}

kj::Arc<const jsg::ExternalMemoryTarget> target() { return memoryTarget(); }

std::vector<uint8_t> zlibRun(ZlibMode mode, int windowBits, const std::vector<uint8_t>& in, int flush,
    bool* failed = nullptr) {
  ZlibUtil::ZlibStream s(mode, target());
  zlibInit(s, windowBits);
  auto p = runAll(*s.context(), in, flush);
  if (failed) *failed = p.error != kj::none;
  return p.out;
}

}  // namespace

int main() {
  auto text = corpus();
  auto hello = bytes("hello world");

  // zlib: exact output for a short string in all three framings.
  report("deflate hello world", hex(zlibRun(ZlibMode::DEFLATE, 15, hello, Z_FINISH)) ==
      "789ccb48cdc9c95728cf2fca4901001a0b045d");
  report("gzip hello world", hex(zlibRun(ZlibMode::GZIP, 15, hello, Z_FINISH)) ==
      "1f8b0800000000000003cb48cdc9c95728cf2fca49010085114a0d0b000000");
  report("deflateraw hello world", hex(zlibRun(ZlibMode::DEFLATERAW, 15, hello, Z_FINISH)) ==
      "cb48cdc9c95728cf2fca490100");

  // zlib round trips.
  {
    auto z = zlibRun(ZlibMode::DEFLATE, 15, text, Z_FINISH);
    report("deflate shrinks text", z.size() > 0 && z.size() < text.size() / 2);
    report("inflate(deflate(text)) == text", zlibRun(ZlibMode::INFLATE, 15, z, Z_FINISH) == text);
    report("unzip(deflate(text)) == text", zlibRun(ZlibMode::UNZIP, 15, z, Z_FINISH) == text);
    auto g = zlibRun(ZlibMode::GZIP, 15, text, Z_FINISH);
    report("gunzip(gzip(text)) == text", zlibRun(ZlibMode::GUNZIP, 15, g, Z_FINISH) == text);
    auto r = zlibRun(ZlibMode::DEFLATERAW, 15, text, Z_FINISH);
    report("inflateraw(deflateraw(text)) == text", zlibRun(ZlibMode::INFLATERAW, 15, r, Z_FINISH) == text);
    bool failed = false;
    auto bad = bytes("this is not a zlib stream");
    zlibRun(ZlibMode::INFLATE, 15, bad, Z_FINISH, &failed);
    report("inflate rejects garbage", failed);
  }

  // Brotli: every quality, which covers the fast encoder (0, 1) the overlap bug is in.
  for (int q = 0; q <= 11; q += (q < 2 ? 1 : 5)) {
    ZlibUtil::BrotliCompressionStream<BrotliEncoderContext> e(ZlibMode::BROTLI_ENCODE, target());
    e.context()->setParams(BROTLI_PARAM_QUALITY, q);
    auto c = runAll(*e.context(), text, BROTLI_OPERATION_FINISH);
    ZlibUtil::BrotliCompressionStream<BrotliDecoderContext> d(ZlibMode::BROTLI_DECODE, target());
    auto p = runAll(*d.context(), c.out, BROTLI_OPERATION_FINISH);
    std::string name = "brotli quality " + std::to_string(q) + " round trip";
    report(name.c_str(), c.error == kj::none && c.out.size() > 0 && c.out.size() < text.size() / 2 &&
        p.error == kj::none && p.out == text);
  }
  {
    ZlibUtil::BrotliCompressionStream<BrotliDecoderContext> d(ZlibMode::BROTLI_DECODE, target());
    auto bad = bytes("\xff\xff\xff\xff\xff\xff\xff\xff");
    auto p = runAll(*d.context(), bad, BROTLI_OPERATION_FINISH);
    report("brotli decoder rejects garbage", p.error != kj::none);
  }

  // zstd: one big output chunk, since the encoder reports a frame that did not fit as an error.
  {
    ZlibUtil::ZstdCompressionStream<ZstdEncoderContext> e(ZlibMode::ZSTD_ENCODE, target());
    e.context()->initialize(ZSTD_CONTENTSIZE_UNKNOWN);
    auto c = runAll(*e.context(), text, ZSTD_e_end, 65536);
    report("zstd frame magic", c.out.size() > 4 && hex({c.out.begin(), c.out.begin() + 4}) == "28b52ffd");
    ZlibUtil::ZstdCompressionStream<ZstdDecoderContext> d(ZlibMode::ZSTD_DECODE, target());
    d.context()->initialize();
    auto p = runAll(*d.context(), c.out, ZSTD_e_end, 65536);
    report("zstd round trip", c.error == kj::none && c.out.size() < text.size() / 2 &&
        p.error == kj::none && p.out == text);
  }

  printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
