// Fuzzes workerd's UUID code (src/workerd/util/uuid.c++). The first input byte picks the entry point:
//   0: UUID::fromString on the rest (8-4-4-4-12 hex), with string/parts round trips on success
//   1: UUID::fromUpperLower on the next 16 bytes, then toString and back through fromString
//   2: randomUUID with an EntropySource that replays the rest (zero-padded), checking the v4 layout
#include <workerd/util/uuid.h>

#include <kj/array.h>
#include <kj/compat/http.h>
#include <kj/string.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

using workerd::UUID;

namespace {

class ReplayEntropy final: public kj::EntropySource {
 public:
  ReplayEntropy(const uint8_t* data, size_t size): data(data), size(size) {}
  void generate(kj::ArrayPtr<kj::byte> buffer) override {
    for (size_t i = 0; i < buffer.size(); i++) {
      buffer[i] = i < size ? data[i] : 0;
    }
  }

 private:
  const uint8_t* data;
  size_t size;
};

void checkRoundTrip(const UUID& uuid) {
  kj::String out = uuid.toString();
  if (out.size() != 36) abort();
  KJ_IF_SOME(again, UUID::fromString(out)) {
    if (!(again == uuid)) abort();
  } else {
    abort();
  }
  KJ_IF_SOME(viaParts, UUID::fromUpperLower(uuid.getUpper(), uuid.getLower())) {
    if (!(viaParts == uuid)) abort();
  } else {
    abort();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) return -1;
  uint8_t mode = data[0] % 3;
  data++;
  size--;

  if (mode == 0) {
    kj::String str = kj::heapString(reinterpret_cast<const char*>(data), size);
    KJ_IF_SOME(uuid, UUID::fromString(str)) {
      checkRoundTrip(uuid);
    }
  } else if (mode == 1) {
    if (size < 16) return -1;
    uint64_t upper, lower;
    memcpy(&upper, data, 8);
    memcpy(&lower, data + 8, 8);
    KJ_IF_SOME(uuid, UUID::fromUpperLower(upper, lower)) {
      if (uuid.getUpper() != upper || uuid.getLower() != lower) abort();
      checkRoundTrip(uuid);
    } else if (upper != 0 || lower != 0) {
      abort();
    }
  } else {
    ReplayEntropy entropy(data, size);
    kj::String id = workerd::randomUUID(entropy);
    if (id.size() != 36 || id[14] != '4') abort();
    char variant = id[19];
    if (variant != '8' && variant != '9' && variant != 'a' && variant != 'b') abort();
    KJ_IF_SOME(uuid, UUID::fromString(id)) {
      if (uuid.toString() != id) abort();
    } else {
      abort();
    }
  }
  return 0;
}
