# Site settings — override on the command line, in a git-ignored local.mk,
# or in a parent Makefile that sets them before `include`-ing this one.
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

CORE_BINS := iris_fetch weather_fetch brightness_ctl
ALL_BINS  := $(CORE_BINS) $(EXTRA_BINS)

.PHONY: all build build-native deploy config colors restart clean

all: build deploy

build:
	cmake -S $(SRC_DIR) -B $(BUILD) -DCMAKE_TOOLCHAIN_FILE=$(abspath $(PUB))/aarch64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)
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
