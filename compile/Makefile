# Deliberately simpler than php-wasm-compiler's Makefile (no matrix.json,
# no JSPI/asyncify variants, no config.yaml-driven extension matrix): this
# project has two fixed-version, dead-upstream libraries (libmad,
# libid3tag) and one purpose-built final module, not dozens of extensions
# across PHP versions. Revisit if the lib count grows enough to justify
# that machinery (see CLAUDE.md).

# libmad/libid3tag: last real release 0.15.1b (2004, SourceForge "mad"
# project) — a fixed pin, not "latest"-tracking (see each Dockerfile's own
# comment for why).
LIBMAD_VERSION := 0.15.1b
LIBID3TAG_VERSION := 0.15.1b

# audiowaveform itself, unlike libmad/libid3tag, is actively maintained
# with real GitHub tags (verified via `gh api repos/bbc/audiowaveform/tags`
# — latest is 1.10.3) — a "latest"-tracking mechanism like
# php-wasm-compiler's matrix.json/update-lib-versions.mjs would make sense
# here eventually, but is deliberately not built yet for a single pinned
# version (see this repo's CLAUDE.md on deliberately not carrying over
# that whole apparatus until the lib/source count justifies it).
AUDIOWAVEFORM_VERSION := 1.10.3

# See php-wasm-compiler's Makefile for why this marker-file pattern matters
# (without it, GNU Make treats base-image as freshly rebuilt on every
# invocation, cascading into rebuilding every lib every time).
base-image: base-image/.ready
base-image/.ready: base-image/Dockerfile
	cd base-image; docker build -f ./Dockerfile -t kirigami-audiowaveform-wasm:base .
	touch base-image/.ready

clean-base-image:
	rm -f base-image/.ready

libmad: libmad/dist/root/lib/lib/libmad.a
libmad/dist/root/lib/lib/libmad.a: base-image
	docker build -f ./libmad/Dockerfile -t kirigami-audiowaveform-wasm:libmad . --build-arg LIBMAD_VERSION=$(LIBMAD_VERSION)
	mkdir -p ./libmad/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libmad) && docker cp $$CID:/root/lib/lib ./libmad/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libmad) && docker cp $$CID:/root/lib/include ./libmad/dist/root/lib && docker rm $$CID

libid3tag: libid3tag/dist/root/lib/lib/libid3tag.a
libid3tag/dist/root/lib/lib/libid3tag.a: base-image
	docker build -f ./libid3tag/Dockerfile -t kirigami-audiowaveform-wasm:libid3tag . --build-arg LIBID3TAG_VERSION=$(LIBID3TAG_VERSION)
	mkdir -p ./libid3tag/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libid3tag) && docker cp $$CID:/root/lib/lib ./libid3tag/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libid3tag) && docker cp $$CID:/root/lib/include ./libid3tag/dist/root/lib && docker rm $$CID

# Final module: audiowaveform's extracted core (CLAUDE.md decision 8) +
# our own Embind wrapper (src/binding.cpp), linked against libmad/libid3tag.
audiowaveform-wasm: libmad libid3tag
	# Build context is the repo root (..), not compile/ — same convention
	# php-wasm-compiler's build.js uses for its PHP Dockerfile — so this
	# Dockerfile can COPY both compile/src/binding.cpp and the repo-root
	# patches/ directory (CLAUDE.md decision 12's convention).
	docker build -f ./audiowaveform/Dockerfile -t kirigami-audiowaveform-wasm:core .. --build-arg AUDIOWAVEFORM_VERSION=$(AUDIOWAVEFORM_VERSION)
	mkdir -p ../node-builds
	CID=$$(docker create kirigami-audiowaveform-wasm:core) && docker cp $$CID:/root/out/. ../node-builds/ && docker rm $$CID

all: audiowaveform-wasm

clean:
	rm -rf libmad/dist libid3tag/dist

.PHONY: base-image clean-base-image all clean
