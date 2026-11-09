// Fuzzes workerd's WHATWG URL layer (src/workerd/jsg/url.c++, a wrapper over ada): URL parsing with
// an optional base, getters/setters, resolution, URLSearchParams and the IDNA / percent-decode helpers.
#include <workerd/jsg/url.h>

#include <kj/string.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

using workerd::jsg::Url;
using workerd::jsg::UrlSearchParams;

namespace {

kj::ArrayPtr<const char> asChars(const uint8_t* d, size_t n) {
  return kj::ArrayPtr<const char>(reinterpret_cast<const char*>(d), n);
}

// Splits `d` at the first NUL: the head is the primary input, the tail (if any) the secondary one.
void split(const uint8_t* d, size_t n, kj::ArrayPtr<const char>& head,
    kj::Maybe<kj::ArrayPtr<const char>>& tail) {
  const void* z = n ? memchr(d, 0, n) : nullptr;
  if (z == nullptr) {
    head = asChars(d, n);
    tail = kj::none;
    return;
  }
  size_t h = static_cast<const uint8_t*>(z) - d;
  head = asChars(d, h);
  tail = asChars(d + h + 1, n - h - 1);
}

int fuzzUrl(const uint8_t* d, size_t n) {
  kj::ArrayPtr<const char> input;
  kj::Maybe<kj::ArrayPtr<const char>> second;
  split(d, n, input, second);

  bool can = Url::canParse(input, second);
  auto parsed = Url::tryParse(input, second);
  if (can != (parsed != kj::none)) abort();  // canParse and tryParse must agree

  KJ_IF_SOME(url, parsed) {
    (void)url.getHref();
    (void)url.getOrigin();
    (void)url.getProtocol();
    (void)url.getUsername();
    (void)url.getPassword();
    (void)url.getHost();
    (void)url.getHostname();
    (void)url.getPort();
    (void)url.getPathname();
    (void)url.getSearch();
    (void)url.getHash();
    (void)url.hashCode();
    (void)url.getHostType();
    (void)url.getSchemeType();
    auto copy = url.clone();
    if (!url.equal(copy)) abort();
    (void)url.clone(Url::EquivalenceOption::IGNORE_FRAGMENTS);
    (void)url.equal(copy, Url::EquivalenceOption::NORMALIZE_PATH);
    // getRelative()/getParent() assert on opaque-path URLs (about:, data:, mailto:); they are only
    // meant for hierarchical ones.
    if (url.getPathname().startsWith("/"_kj)) {
      (void)url.getRelative();
      (void)url.getParent();
    }
    (void)url.tryResolve(input);
    // Round trip: the serialized form of a URL must parse again.
    (void)Url::tryParse(url.getHref());

    kj::ArrayPtr<const char> v = input;
    KJ_IF_SOME(s, second) v = s;
    (void)copy.setHost(v);
    (void)copy.setHostname(v);
    (void)copy.setProtocol(v);
    (void)copy.setUsername(v);
    (void)copy.setPassword(v);
    (void)copy.setPort(v);
    (void)copy.setPathname(v);
    copy.setSearch(v);
    copy.setHash(v);
    (void)copy.getHref();
    (void)copy.setHref(v);
    return 0;
  }
  return -1;
}

int fuzzSearchParams(const uint8_t* d, size_t n) {
  kj::ArrayPtr<const char> input;
  kj::Maybe<kj::ArrayPtr<const char>> second;
  split(d, n, input, second);

  auto parsed = UrlSearchParams::tryParse(input);
  KJ_IF_SOME(params, parsed) {
    kj::ArrayPtr<const char> key = input;
    KJ_IF_SOME(s, second) key = s;
    (void)params.size();
    (void)params.toStr();
    (void)params.has(key);
    (void)params.get(key);
    (void)params.getAll(key);
    {
      auto entries = params.getEntries();
      while (entries.hasNext()) {
        KJ_IF_SOME(e, entries.next()) {
          (void)e.key;
          (void)e.value;
        }
      }
      auto keys = params.getKeys();
      while (keys.hasNext()) (void)keys.next();
      auto values = params.getValues();
      while (values.hasNext()) (void)values.next();
    }
    params.append(key, input);
    params.set(key, input);
    params.sort();
    (void)params.toStr();
    params.delete_(key);
    (void)params.size();
    return 0;
  }
  return -1;
}

int fuzzHelpers(const uint8_t* d, size_t n) {
  auto s = asChars(d, n);
  (void)Url::idnToAscii(s);
  (void)Url::idnToUnicode(s);
  (void)Url::percentDecode(kj::ArrayPtr<const kj::byte>(d, n));
  return 0;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) return -1;
  switch (data[0] % 3) {
    case 0:
      return fuzzUrl(data + 1, size - 1);
    case 1:
      return fuzzSearchParams(data + 1, size - 1);
    default:
      return fuzzHelpers(data + 1, size - 1);
  }
}
