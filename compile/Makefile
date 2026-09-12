MAKEFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
MAKEFILE_DIR := $(patsubst %/,%,$(dir $(MAKEFILE_PATH)))

# Versions now come from matrix.json (not hand-synced Makefile variables),
# same mechanism as php-wasm-compiler's MATRIX_VERSION — requested by the
# user 2026-09-12 to apply the same version-tracking discipline here, even
# for libmad/libid3tag's dead-upstream fixed pins (see matrix.json's own
# notes on why those two don't have a real "latest" to track).
MATRIX_VERSION = $(shell node $(MAKEFILE_DIR)/matrix-version.mjs $(1))

LIBMAD_VERSION := $(call MATRIX_VERSION,libmad)
LIBID3TAG_VERSION := $(call MATRIX_VERSION,libid3tag)
LIBZ_VERSION := $(call MATRIX_VERSION,libz)
LIBSNDFILE_VERSION := $(call MATRIX_VERSION,libsndfile)
LIBOGG_VERSION := $(call MATRIX_VERSION,libogg)
LIBFLAC_VERSION := $(call MATRIX_VERSION,libflac)
LIBVORBIS_VERSION := $(call MATRIX_VERSION,libvorbis)
LIBOPUS_VERSION := $(call MATRIX_VERSION,libopus)
LIBFAAD2_VERSION := $(call MATRIX_VERSION,libfaad2)
NESTEGG_VERSION := $(call MATRIX_VERSION,nestegg)
AUDIOWAVEFORM_VERSION := $(call MATRIX_VERSION,audiowaveform)

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
	# Context is the repo root (..), not compile/ — needed so the
	# Dockerfile can COPY the repo-root patches/libmad/ directory
	# (CLAUDE.md decision 12's convention).
	docker build -f ./libmad/Dockerfile -t kirigami-audiowaveform-wasm:libmad .. --build-arg LIBMAD_VERSION=$(LIBMAD_VERSION)
	mkdir -p ./libmad/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libmad) && docker cp $$CID:/root/lib/lib ./libmad/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libmad) && docker cp $$CID:/root/lib/include ./libmad/dist/root/lib && docker rm $$CID

libz: libz/dist/root/lib/lib/libz.a
libz/dist/root/lib/lib/libz.a: base-image
	docker build -f ./libz/Dockerfile -t kirigami-audiowaveform-wasm:libz . --build-arg LIBZ_VERSION=$(LIBZ_VERSION)
	mkdir -p ./libz/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libz) && docker cp $$CID:/root/lib/lib ./libz/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libz) && docker cp $$CID:/root/lib/include ./libz/dist/root/lib && docker rm $$CID

# Needs libz (matrix.json's libz note — libid3tag's configure hard-requires
# zlib.h, no --without-zlib option exists).
libid3tag: libid3tag/dist/root/lib/lib/libid3tag.a
libid3tag/dist/root/lib/lib/libid3tag.a: base-image libz
	docker build -f ./libid3tag/Dockerfile -t kirigami-audiowaveform-wasm:libid3tag .. --build-arg LIBID3TAG_VERSION=$(LIBID3TAG_VERSION)
	mkdir -p ./libid3tag/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libid3tag) && docker cp $$CID:/root/lib/lib ./libid3tag/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libid3tag) && docker cp $$CID:/root/lib/include ./libid3tag/dist/root/lib && docker rm $$CID

libogg: libogg/dist/root/lib/lib/libogg.a
libogg/dist/root/lib/lib/libogg.a: base-image
	docker build -f ./libogg/Dockerfile -t kirigami-audiowaveform-wasm:libogg . --build-arg LIBOGG_VERSION=$(LIBOGG_VERSION)
	mkdir -p ./libogg/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libogg) && docker cp $$CID:/root/lib/lib ./libogg/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libogg) && docker cp $$CID:/root/lib/include ./libogg/dist/root/lib && docker rm $$CID

# WITH_OGG=OFF — native FLAC only, no libogg dependency (compile/libflac/Dockerfile).
libflac: libflac/dist/root/lib/lib/libFLAC.a
libflac/dist/root/lib/lib/libFLAC.a: base-image
	docker build -f ./libflac/Dockerfile -t kirigami-audiowaveform-wasm:libflac . --build-arg LIBFLAC_VERSION=$(LIBFLAC_VERSION)
	mkdir -p ./libflac/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libflac) && docker cp $$CID:/root/lib/lib ./libflac/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libflac) && docker cp $$CID:/root/lib/include ./libflac/dist/root/lib && docker rm $$CID

libvorbis: libvorbis/dist/root/lib/lib/libvorbis.a
libvorbis/dist/root/lib/lib/libvorbis.a: base-image libogg
	docker build -f ./libvorbis/Dockerfile -t kirigami-audiowaveform-wasm:libvorbis . --build-arg LIBVORBIS_VERSION=$(LIBVORBIS_VERSION)
	mkdir -p ./libvorbis/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libvorbis) && docker cp $$CID:/root/lib/lib ./libvorbis/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libvorbis) && docker cp $$CID:/root/lib/include ./libvorbis/dist/root/lib && docker rm $$CID

libopus: libopus/dist/root/lib/lib/libopus.a
libopus/dist/root/lib/lib/libopus.a: base-image
	docker build -f ./libopus/Dockerfile -t kirigami-audiowaveform-wasm:libopus . --build-arg LIBOPUS_VERSION=$(LIBOPUS_VERSION)
	mkdir -p ./libopus/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libopus) && docker cp $$CID:/root/lib/lib ./libopus/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libopus) && docker cp $$CID:/root/lib/include ./libopus/dist/root/lib && docker rm $$CID

# WAV/AIFF/RAW/FLAC/Ogg-Vorbis/Ogg-Opus (matrix.json's libsndfile note).
libsndfile: libsndfile/dist/root/lib/lib/libsndfile.a
libsndfile/dist/root/lib/lib/libsndfile.a: base-image libogg libflac libvorbis libopus
	docker build -f ./libsndfile/Dockerfile -t kirigami-audiowaveform-wasm:libsndfile . --build-arg LIBSNDFILE_VERSION=$(LIBSNDFILE_VERSION)
	mkdir -p ./libsndfile/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libsndfile) && docker cp $$CID:/root/lib/lib ./libsndfile/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libsndfile) && docker cp $$CID:/root/lib/include ./libsndfile/dist/root/lib && docker rm $$CID

# M4A/AAC (CLAUDE.md decision 9's correction #2). Only the core decoder
# library is built here; compile/audiowaveform/Dockerfile pulls the
# frontend/mp4read.c demuxer files from this same image's source tree.
libfaad2: libfaad2/dist/root/lib/lib/libfaad.a
libfaad2/dist/root/lib/lib/libfaad.a: base-image
	docker build -f ./libfaad2/Dockerfile -t kirigami-audiowaveform-wasm:libfaad2 . --build-arg LIBFAAD2_VERSION=$(LIBFAAD2_VERSION)
	mkdir -p ./libfaad2/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:libfaad2) && docker cp $$CID:/root/lib/lib ./libfaad2/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:libfaad2) && docker cp $$CID:/root/lib/include ./libfaad2/dist/root/lib && docker rm $$CID

# WebM/Matroska demuxer (decision 19) — decoding reuses libvorbis/libopus,
# already vendored above, so this only needs base-image.
nestegg: nestegg/dist/root/lib/lib/libnestegg.a
nestegg/dist/root/lib/lib/libnestegg.a: base-image
	docker build -f ./nestegg/Dockerfile -t kirigami-audiowaveform-wasm:nestegg . --build-arg NESTEGG_VERSION=$(NESTEGG_VERSION)
	mkdir -p ./nestegg/dist/root/lib
	CID=$$(docker create kirigami-audiowaveform-wasm:nestegg) && docker cp $$CID:/root/lib/lib ./nestegg/dist/root/lib && docker rm $$CID
	CID=$$(docker create kirigami-audiowaveform-wasm:nestegg) && docker cp $$CID:/root/lib/include ./nestegg/dist/root/lib && docker rm $$CID

# Final module: audiowaveform's extracted core (CLAUDE.md decision 8) +
# our own Embind wrapper (src/binding.cpp), linked against
# libmad/libid3tag/libsndfile (which itself bundles libogg/libflac/
# libvorbis/libopus)/libfaad2/nestegg.
audiowaveform-wasm: libmad libid3tag libsndfile libfaad2 nestegg
	# Build context is the repo root (..), not compile/ — same convention
	# php-wasm-compiler's build.js uses for its PHP Dockerfile — so this
	# Dockerfile can COPY both compile/src/binding.cpp and the repo-root
	# patches/ directory (CLAUDE.md decision 12's convention).
	docker build -f ./audiowaveform/Dockerfile -t kirigami-audiowaveform-wasm:core .. --build-arg AUDIOWAVEFORM_VERSION=$(AUDIOWAVEFORM_VERSION)
	mkdir -p ../node-builds
	CID=$$(docker create kirigami-audiowaveform-wasm:core) && docker cp $$CID:/root/out/. ../node-builds/ && docker rm $$CID

all: audiowaveform-wasm

clean:
	rm -rf libmad/dist libid3tag/dist libz/dist libsndfile/dist libogg/dist libflac/dist libvorbis/dist libopus/dist libfaad2/dist nestegg/dist

.PHONY: base-image clean-base-image all clean
