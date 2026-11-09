// Fuzzes workerd's MIME type parser/serializer (src/workerd/util/mimetype.c++, WHATWG MIME Sniffing).
#include <workerd/util/mimetype.h>

#include <kj/string.h>

#include <cstdint>
#include <cstdlib>

using workerd::MimeType;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  kj::ArrayPtr<const char> input(reinterpret_cast<const char*>(data), size);
  auto parsed = MimeType::tryParse(input);
  KJ_IF_SOME(mt, parsed) {
    kj::String str = mt.toString();
    (void)mt.essence();
    (void)mt.paramsToString();
    (void)MimeType::isXml(mt);
    (void)MimeType::isJson(mt);
    (void)MimeType::isText(mt);
    (void)MimeType::isJavascript(mt);
    (void)MimeType::isFont(mt);
    (void)MimeType::isImage(mt);
    (void)MimeType::isVideo(mt);
    (void)MimeType::isAudio(mt);
    for (auto& p: mt.params()) {
      (void)p.key;
      (void)p.value;
    }
    // The serialization of a parsed MIME type must parse back to the same essence.
    KJ_IF_SOME(again, MimeType::tryParse(str.asPtr())) {
      if (!(again == mt)) abort();
    } else {
      abort();
    }
    auto copy = mt.clone();
    (void)copy.addParam(input, input);
    copy.eraseParam(kj::heapString(input));
    (void)mt.clone(MimeType::IGNORE_PARAMS);
    return 0;
  }
  auto ignored = MimeType::tryParse(input, MimeType::IGNORE_PARAMS);
  return ignored == kj::none ? -1 : 0;
}
