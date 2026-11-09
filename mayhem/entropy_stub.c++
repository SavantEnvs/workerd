// workerd::getEntropy lives in util/entropy.c++, which pulls in ncrypto + OpenSSL; the harnesses
// only need a working random source for the code paths they reach (sentry, uuid).
#include <kj/array.h>
#include <sys/random.h>

namespace workerd {
void getEntropy(kj::ArrayPtr<kj::byte> buf) {
  size_t off = 0;
  while (off < buf.size()) {
    ssize_t r = getrandom(buf.begin() + off, buf.size() - off, 0);
    if (r > 0) off += static_cast<size_t>(r);
  }
}
}  // namespace workerd
