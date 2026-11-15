.PHONY: package verify test

BASE_PACKAGE ?= release/BB-BOB-r35-app-base.fwsc
TIMBRES ?= release/timbres.bank
ART ?= release/art.bank
LOADER ?= release/BB-BOB-r35-loader.bin
OUTPUT ?= release/BB-BOB-r35-hub.fwsc

package:
	PYTHONDONTWRITEBYTECODE=1 python3 tools/embed_banks.py \
	  --base "$(BASE_PACKAGE)" --timbres "$(TIMBRES)" --art "$(ART)" \
	  --loader "$(LOADER)" --output "$(OUTPUT)"

verify:
	PYTHONDONTWRITEBYTECODE=1 python3 tools/verify_release.py \
	  --package "$(OUTPUT)" --manifest release/RELEASE.json

test: verify
	mkdir -p build/host
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-unused-function -O2 tests/loader_test.c -o build/host/loader-test
	build/host/loader-test "$(BASE_PACKAGE)" "$(OUTPUT)"
