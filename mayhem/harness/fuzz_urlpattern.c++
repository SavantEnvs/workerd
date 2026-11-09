// Fuzzes workerd's URLPattern compiler (src/workerd/jsg/url.c++): pattern string parsing/tokenizing,
// per-component regex generation and init processing.
#include <workerd/jsg/url.h>

#include <kj/string.h>

#include <cstdint>
#include <cstring>

using workerd::jsg::UrlPattern;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) return -1;
  const char* p = reinterpret_cast<const char*>(data + 1);
  size_t n = size - 1;
  const void* z = n ? memchr(p, 0, n) : nullptr;
  size_t headLen = z ? static_cast<const char*>(z) - p : n;
  kj::String pattern = kj::heapString(p, headLen);
  kj::String baseStr = z ? kj::heapString(p + headLen + 1, n - headLen - 1) : kj::String();

  UrlPattern::CompileOptions opts;
  opts.ignoreCase = (data[0] & 1) != 0;
  if (data[0] & 2) opts.baseUrl = baseStr.asPtr();

  if (data[0] & 4) {
    // Init-dictionary form: spread the pattern across components.
    UrlPattern::Init init;
    switch ((data[0] >> 3) % 8) {
      case 0: init.protocol = kj::mv(pattern); break;
      case 1: init.username = kj::mv(pattern); break;
      case 2: init.password = kj::mv(pattern); break;
      case 3: init.hostname = kj::mv(pattern); break;
      case 4: init.port = kj::mv(pattern); break;
      case 5: init.pathname = kj::mv(pattern); break;
      case 6: init.search = kj::mv(pattern); break;
      default: init.hash = kj::mv(pattern); break;
    }
    if (baseStr.size() > 0) init.baseUrl = kj::mv(baseStr);
    auto result = UrlPattern::tryCompile(kj::mv(init), opts);
    KJ_SWITCH_ONEOF(result) {
      KJ_CASE_ONEOF(pat, UrlPattern) {
        (void)pat.getPathname().getRegex();
        return 0;
      }
      KJ_CASE_ONEOF(err, kj::String) {
        return -1;
      }
    }
    return -1;
  }

  auto result = UrlPattern::tryCompile(pattern, opts);
  KJ_SWITCH_ONEOF(result) {
    KJ_CASE_ONEOF(pat, UrlPattern) {
      (void)pat.getProtocol().getRegex();
      (void)pat.getHostname().getRegex();
      (void)pat.getPathname().getRegex();
      (void)pat.getSearch().getRegex();
      (void)pat.getHash().getRegex();
      return 0;
    }
    KJ_CASE_ONEOF(err, kj::String) {
      return -1;
    }
  }
  return -1;
}
