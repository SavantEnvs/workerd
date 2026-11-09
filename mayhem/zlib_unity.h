// Shared by the fuzz harness and the known-answer test: compiles the real zlib/Brotli/zstd handle of
// workerd (src/workerd/api/node/zlib-util.c++, src/workerd/api/compression.c++) into the including
// translation unit, with V8 replaced by v8_stubs.c++ and a fake ArrayBuffer (v8_fake.h).
#pragma once

#include <kj/array.h>
#include <kj/debug.h>
#include <kj/exception.h>
#include <kj/vector.h>

#include <brotli/decode.h>
#include <brotli/encode.h>
#include <zlib.h>
#include <zstd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <v8.h>

// Compiled with -fno-access-control: the harness sets up the handle (context(), `initialized`) itself in
// place of the JS initialize() methods, which need a live isolate; the write path under test is untouched.
#include <workerd/api/node/zlib-util.h>
#include <workerd/api/compression.h>

#include <workerd/api/node/zlib-util.c++>
#include <workerd/api/compression.c++>

#include "v8_fake.h"

namespace {

using namespace workerd;
using namespace workerd::api;
using namespace workerd::api::node;

jsg::JsBufferSource wrap(mayhem::FakeBuffer* buf) {
  v8::Local<v8::Value> local;
  static_assert(sizeof(local) == sizeof(buf));
  memcpy(&local, &buf, sizeof(buf));
  return jsg::JsBufferSource(local);
}

jsg::Lock& fakeLock() {
  alignas(jsg::Lock) static char storage[sizeof(jsg::Lock)];
  return *reinterpret_cast<jsg::Lock*>(storage);
}

kj::Arc<const jsg::ExternalMemoryTarget> memoryTarget() {
  return kj::arc<jsg::ExternalMemoryTarget>(nullptr);
}


}  // namespace
