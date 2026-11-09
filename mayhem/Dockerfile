# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form, PREFERRED) — builds ON TOP of the org C/C++ base
# image (ghcr.io/savantenvs/base). The base already provides debian + clang/llvm +
# libFuzzer/afl++ + the identities + the build-contract ENV, so this only adds the
# per-commit build (built on top of the prebuilt base, rather than building the toolchain in-image).
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

# The org base, pinned to a fixed DATE-tagged base (YYYYMMDD; shared by all integrations
# minted against it). Use the current dated tag; pin by digest in production: ...base@sha256:<digest>.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as an ENV — an
# empty value breaks clang ("must be a non-negative decimal integer") both here and for
# any in-image rebuild. build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: the base exports SANITIZER_FLAGS (ASan+UBSan, halting) as the default.
# To change it for a build (e.g. ASan-only, or add MSan): --build-arg SANITIZER_FLAGS="...".
# Declare it WITHOUT a default (no '='): an undefaulted ARG lets the base ENV flow through to
# build.sh when not passed, and an explicit --build-arg overrides it. (An empty default — `=` —
# would instead shadow the base ENV and silently wipe the sanitizers.)
ARG SANITIZER_FLAGS
# Coverage (optional, off by default): instrument the TEST build to measure how much of the project
# the suite exercises — --build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping".
# Empty by default (no effect on the normal oracle build); build.sh appends it to the test build only.
ARG COVERAGE_FLAGS=
# Build parallelism (optional). build.sh falls back to nproc when unset: --build-arg MAYHEM_JOBS=8
ARG MAYHEM_JOBS=

# Build deps. The full workerd build (Bazel + V8) is not reproducible air-gapped, so the harnesses compile
# real workerd sources (src/workerd/{jsg/url,util/mimetype,util/uuid,util/sentry,util/strings,
# util/thread-scopes}.c++) against these pinned, uninstrumented, DWARF-3 static libraries (SPEC §6.2 item 19).
USER root
# ICU (jsg/url.c++ uses unicode/uchar.h + utf8.h; workerd ships ICU inside V8).
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends libicu-dev \
 && rm -rf /var/lib/apt/lists/*

# capnproto: kj + capnp libraries (workerd's own pin is the commit in build/deps/gen/deps.MODULE.bazel's
# capnp archive; this is the same tarball).
ARG CAPNP_COMMIT=9cdd0a4313fa85420ca071f058cb1a6a73eb5f28
ARG CAPNP_SHA256=661a847ae18b099424d0ebc51456c75e369543484caf743769f81648a386484d
RUN d=/opt/toolchains/deps/capnproto-${CAPNP_COMMIT} && mkdir -p "$d/src" /tmp/dep-build \
 && curl -fsSL -o /tmp/dep.tgz https://github.com/capnproto/capnproto/tarball/${CAPNP_COMMIT} \
 && echo "${CAPNP_SHA256}  /tmp/dep.tgz" | sha256sum -c - \
 && tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src" && cd /tmp/dep-build \
 && env -u LIB_FUZZING_ENGINE cmake -G Ninja -S "$d/src/c++" -B . -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$d" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
      -DCMAKE_C_FLAGS="-O2 -g -gdwarf-3" -DCMAKE_CXX_FLAGS="-O2 -g -gdwarf-3" \
 && ninja install && cd / && rm -rf /tmp/dep-build /tmp/dep.tgz && chmod -R a+rX "$d"

# V8 public headers only (jsg/memory.h includes v8.h; nothing links against V8).
ARG V8_VERSION=15.8.62.5
ARG V8_COMMIT=b17064889ef49a632dd09bead566cf50887886bf
RUN d=/opt/toolchains/deps/v8-headers-${V8_VERSION} && mkdir -p "$d/src" && cd "$d/src" \
 && git init -q . && git remote add origin https://github.com/v8/v8 \
 && git config core.sparseCheckout true && echo "/include/" > .git/info/sparse-checkout \
 && git fetch -q --depth 1 --filter=blob:none origin ${V8_COMMIT} && git checkout -q FETCH_HEAD \
 && [ "$(git rev-parse HEAD)" = "${V8_COMMIT}" ] && rm -rf .git && ln -s src/include "$d/include" \
 && chmod -R a+rX "$d"

# ada URL parser (single-header release), built as a static library.
ARG ADA_VERSION=4.0.0
ARG ADA_SHA256=d7fa5aea7b3805c91111231d1aaaeefc39ae20562a9f599e13c22471ee576b9f
RUN d=/opt/toolchains/deps/ada-${ADA_VERSION} && mkdir -p "$d/src" "$d/include" "$d/lib" \
 && curl -fsSL -o /tmp/ada.zip https://github.com/ada-url/ada/releases/download/v${ADA_VERSION}/singleheader.zip \
 && echo "${ADA_SHA256}  /tmp/ada.zip" | sha256sum -c - \
 && unzip -q -o /tmp/ada.zip -d "$d/src" && cp "$d"/src/ada.h "$d"/src/ada_c.h "$d/include/" \
 && clang++ -std=c++20 -O2 -g -gdwarf-3 -c "$d/src/ada.cpp" -o /tmp/ada.o && ar rcs "$d/lib/libada.a" /tmp/ada.o \
 && rm -f /tmp/ada.o /tmp/ada.zip && chmod -R a+rX "$d"

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git
# history) is copied to /mayhem as `mayhem`; build.sh builds in place.
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Anything a Dockerfile step materializes into the tree (downloaded fixtures, generated headers,
# vendored deps) MUST be committed or (re)created by build.sh: rlenv's patch grader runs
# `git clean -ffdX` before every graded build, so a gitignored, untracked file is gone by then and
# every honest patch scores zero (SPEC §6.2 item 16; verify-repo.sh checks it).
#
# OPTIONAL compile-cache pre-fill (SPEC §6.2 item 18). Bases minted with /opt/mayhem/ccache/prefill
# run the build under rlenv's pinned ccache, so the patch grader's first (otherwise cold) rebuild is
# warm — this is what keeps large C/C++ targets inside the build time limit. Older bases fall back to
# the plain build; a missing or stale pre-fill only costs a cold cache. Never enable ccache in
# build.sh or via ENV/PATH, and keep the compiler resolving by name (CC=clang) or nothing is cached.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

# Run the project's functional tests at build time IF the repo ships them (mayhem/test.sh is
# optional — omit it only when the repo has no tests meaningful for RL). A failing suite fails the
# image build, so the commit image is guaranteed to build AND pass its own tests.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
