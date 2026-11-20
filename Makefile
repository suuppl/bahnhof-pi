# Site settings — override on the command line, in a git-ignored local.mk,
# or in a parent Makefile that sets them before `include`-ing this one.
SHELL := /bin/bash
-include local.mk
PI           ?= dietpi@raspberrypi.local
SSH_KEY      ?= .ssh/pi_key
# install dir on the Pi, relative to the Pi user's $HOME (must match APP_DIR in config.h)
APP_DIR      ?= bahnhofstafel
# Path to this (public) repo, and the CMake source dir to build. A parent
# project embedding this repo as a submodule sets PUB := <submodule path>
# and SRC_DIR := . (its own CMakeLists.txt add_subdirectory()s this repo).
PUB          ?= .
SRC_DIR      ?= $(PUB)
# Optional runtime settings and train-line colours, pushed to the Pi on deploy
CONFIG_FILE      ?= $(wildcard bahnhofstafel.conf)
LINE_COLORS_FILE ?= $(wildcard line_colors.json)
# Additional daemons built by a parent project; deployed and started too.
EXTRA_BINS   ?=
CMAKE_ARGS   ?=

BUILD        := build
BUILD_NATIVE := build-native
SSH          := ssh -i $(SSH_KEY) -o StrictHostKeyChecking=no -o ConnectTimeout=30 -o ServerAliveInterval=5
PI_USER      := $(firstword $(subst @, ,$(PI)))
REMOTE       := ~/$(APP_DIR)/bin

# core = iris_fetch + display (bahnhofstafel, deployed separately below);
# bundled plugins are always deployed, PLUGINS in bahnhofstafel.conf picks
# which ones start.
CORE_BINS   := iris_fetch
PLUGIN_BINS := weather_fetch brightness_ctl presence_ctl
ALL_BINS    := $(CORE_BINS) $(PLUGIN_BINS) $(EXTRA_BINS)

# Cross-compilation sysroot: headers + link libraries copied from the Pi
# (git-ignored; see docs/sysroot.md). `make build` fetches it if missing.
SYSROOT             ?= $(abspath $(PUB))/sysroot
SYSROOT_LIBS        := libcurl libxml2 libcjson libfreetype
SYSROOT_HEADERS     := usr/include/curl usr/include/aarch64-linux-gnu/curl \
                       usr/include/libxml2 usr/include/cjson usr/include/freetype2
SYSROOT_PI_PACKAGES := libcurl4-openssl-dev libxml2-dev libcjson-dev libfreetype-dev
SYSROOT_REMOTE_ROOT ?= /

.PHONY: all build build-native deploy config colors restart clean sysroot

all: build deploy

# (re)create the sysroot from the Pi; needs the -dev packages installed there.
# Fetched into sysroot.new first so a failed copy keeps the old one.
sysroot:
	rm -rf "$(SYSROOT).new" && mkdir -p "$(SYSROOT).new"
	set -o pipefail; $(SSH) $(PI) 'cd $(SYSROOT_REMOTE_ROOT) && L=usr/lib/aarch64-linux-gnu; m=; \
		for l in $(SYSROOT_LIBS); do [ -e $$L/$$l.so ] || m="$$m $$l"; done; \
		if [ -n "$$m" ]; then echo "missing on the Pi:$$m -- run: sudo apt install $(SYSROOT_PI_PACKAGES)" >&2; exit 1; fi; \
		tar -czf - $$(for p in $(SYSROOT_HEADERS); do [ -e $$p ] && echo $$p; done) \
		           $$(for l in $(SYSROOT_LIBS); do ls -d $$L/$$l.so*; done)' \
		| tar -xzf - -C "$(SYSROOT).new"
	rm -rf "$(SYSROOT)" && mv "$(SYSROOT).new" "$(SYSROOT)"
	@echo "sysroot ready: $$(ls $(SYSROOT)/usr/lib/aarch64-linux-gnu | tr '\n' ' ')"

$(SYSROOT)/usr/lib/aarch64-linux-gnu/libcurl.so:
	$(MAKE) sysroot

build: | $(SYSROOT)/usr/lib/aarch64-linux-gnu/libcurl.so
	cmake -S $(SRC_DIR) -B $(BUILD) -DCMAKE_TOOLCHAIN_FILE=$(abspath $(PUB))/aarch64-toolchain.cmake -DBAHNHOF_SYSROOT=$(SYSROOT) -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)
	cmake --build $(BUILD) -j$$(nproc)

build-native:
	cmake -S $(SRC_DIR) -B $(BUILD_NATIVE) -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)
	cmake --build $(BUILD_NATIVE) -j$$(nproc)

deploy: build
	# stop via systemd (frees the running binaries and kills the process group)
	$(SSH) $(PI) "sudo systemctl stop bahnhofstafel.service; mkdir -p $(REMOTE)"
	@set -e; for b in $(ALL_BINS); do \
		echo "deploy $$b"; \
		$(SSH) $(PI) "cat > $(REMOTE)/$$b.new && chmod +x $(REMOTE)/$$b.new && mv $(REMOTE)/$$b.new $(REMOTE)/$$b" < $(BUILD)/$$b; \
	done
	gzip -c $(BUILD)/bahnhofstafel | \
		$(SSH) $(PI) "gzip -d > $(REMOTE)/bahnhofstafel.new && chmod +x $(REMOTE)/bahnhofstafel.new && mv $(REMOTE)/bahnhofstafel.new $(REMOTE)/bahnhofstafel"
	# list of extra daemons, read by start.sh
	echo "$(strip $(EXTRA_BINS))" | $(SSH) $(PI) "cat > ~/$(APP_DIR)/extra_daemons"
	# start.sh and service file — always push (may change)
	$(SSH) $(PI) "cat > ~/$(APP_DIR)/start.sh.new && chmod +x ~/$(APP_DIR)/start.sh.new && mv ~/$(APP_DIR)/start.sh.new ~/$(APP_DIR)/start.sh" \
		< $(PUB)/systemd/start.sh
	sed -e "s|@USER@|$(PI_USER)|g" -e "s|@APP_DIR@|$(APP_DIR)|g" $(PUB)/systemd/bahnhofstafel.service | \
		$(SSH) $(PI) "sudo cp /dev/stdin /etc/systemd/system/bahnhofstafel.service && sudo systemctl daemon-reload"
	# runtime settings — optional, read at startup
	$(if $(CONFIG_FILE),$(SSH) $(PI) "cat > ~/$(APP_DIR)/bahnhofstafel.conf" < $(CONFIG_FILE))
	# line colours — optional, the display reloads them when they change
	$(if $(LINE_COLORS_FILE),$(SSH) $(PI) "cat > ~/$(APP_DIR)/line_colors.json" < $(LINE_COLORS_FILE))
	# fonts — push only if missing (unchanging files)
	@for f in NotoEmoji-Regular.ttf Twemoji.Mozilla.ttf; do \
		$(SSH) $(PI) "test -f ~/$(APP_DIR)/fonts/$$f || (mkdir -p ~/$(APP_DIR)/fonts && cat > ~/$(APP_DIR)/fonts/$$f)" < $(PUB)/fonts/$$f; \
	done
	$(SSH) $(PI) "sudo systemctl start bahnhofstafel.service"

# push only bahnhofstafel.conf and restart
config:
	$(SSH) $(PI) "cat > ~/$(APP_DIR)/bahnhofstafel.conf" < $(CONFIG_FILE)
	$(SSH) $(PI) "sudo systemctl restart bahnhofstafel.service"

# push only line_colors.json (no restart needed)
colors:
	$(SSH) $(PI) "cat > ~/$(APP_DIR)/line_colors.json" < $(LINE_COLORS_FILE)

restart:
	$(SSH) $(PI) "sudo systemctl restart bahnhofstafel.service"

clean:
	rm -rf $(BUILD) $(BUILD_NATIVE)
