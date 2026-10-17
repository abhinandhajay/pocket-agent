BUILD_DIR ?= $(CURDIR)/.build
PYTHON ?= $(BUILD_DIR)/python/bin/python
NODE ?= node
BRIDGE_HOME ?= $(if $(wildcard $(BUILD_DIR)/hub-home),$(BUILD_DIR)/hub-home,$(HOME)/.pocket-agent)
RUFF ?= $(BUILD_DIR)/python/bin/ruff
ARDUINO_CLI ?= arduino-cli
FQBN := esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi
SKETCH := $(BUILD_DIR)/sketch/pocket_agent
ARDUINO_CONFIG := $(BUILD_DIR)/toolchain/arduino-cli.yaml

.PHONY: format check-format test check-js check-python check-public toolchain-config check-toolchain stage assets avatar-preview build hub status wifi

format:
	NODE="$(NODE)" $(PYTHON) tools/format.py

check-format:
	NODE="$(NODE)" $(PYTHON) tools/format.py --check

test:
	PYTHON="$(PYTHON)" NODE="$(NODE)" POCKET_AGENT_BUILD="$(BUILD_DIR)" bash tests/run.sh
	bash tests/touch/run-candidate.sh firmware/pocket_agent "$(BUILD_DIR)/touch-contract-test"

check-js:
	@for source in host/bridge/*.mjs tests/*.mjs tests/fixtures/*.mjs tools/review/*.mjs; do test -f "$$source" || continue; $(NODE) --check "$$source" || exit $$?; done

check-python:
	"$(RUFF)" check --select F401,F841 host tests tools

check-public:
	$(PYTHON) tools/check_public.py

toolchain-config:
	$(PYTHON) tools/build.py config --build-dir "$(BUILD_DIR)"

check-toolchain:
	$(PYTHON) tools/build.py check-toolchain --build-dir "$(BUILD_DIR)" --cli "$(ARDUINO_CLI)"

stage:
	$(PYTHON) tools/build.py stage --build-dir "$(BUILD_DIR)"

assets: stage
	$(PYTHON) tools/assets/derive_assets.py "$(SKETCH)/avatar_assets.h"

avatar-preview:
	$(PYTHON) tests/check_avatar.py --preview
	$(PYTHON) tools/review/avatar-preview.py

build: check-toolchain assets
	"$(ARDUINO_CLI)" --config-file "$(ARDUINO_CONFIG)" compile --fqbn "$(FQBN)" --build-property "tools.ctags.pattern=/usr/bin/true" --warnings all --build-path "$(BUILD_DIR)/firmware" "$(SKETCH)"

hub:
	POCKET_AGENT_HOME="$${POCKET_AGENT_HOME:-$(BRIDGE_HOME)}" $(NODE) host/bridge/hub.mjs

status:
	POCKET_AGENT_HOME="$${POCKET_AGENT_HOME:-$(BRIDGE_HOME)}" $(NODE) host/bridge/cli.mjs status

wifi:
	POCKET_AGENT_HOME="$${POCKET_AGENT_HOME:-$(BRIDGE_HOME)}" $(NODE) host/bridge/cli.mjs wifi "$(SSID)"
