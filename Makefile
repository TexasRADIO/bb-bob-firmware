.PHONY: package verify test

BASE_PACKAGE ?= release/BB-BOB-r37-app-base.fwsc
TIMBRES ?= release/timbres.bank
ART ?= release/art.bank
LOADER ?= release/BB-BOB-r37-loader.bin
OUTPUT ?= release/BB-BOB-r37-editor-hub.fwsc

package:
	PYTHONDONTWRITEBYTECODE=1 python3 tools/embed_banks.py \
	  --base "$(BASE_PACKAGE)" --timbres "$(TIMBRES)" --art "$(ART)" \
	  --loader "$(LOADER)" --output "$(OUTPUT)"

verify:
	PYTHONDONTWRITEBYTECODE=1 python3 tools/verify_release.py \
	  --package "$(OUTPUT)" --manifest release/RELEASE.json

build/host/dance-hall-test: tests/dance_hall_test.c $(wildcard src/*.[ch]) build/classic_bank.h build/host/showbill_calls.h
	mkdir -p build/host
	$(CC) -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation -DSW_WITH_CUE=1 -DSW_WITH_ART=1 -DSW_TARGET_SCORE_SMALL=1 -Ibuild/host -Ibuild/target-base-machine -Isrc -Ibuild tests/dance_hall_test.c src/swing.c src/panel.c src/showbill.c src/display.c src/art_bank.c src/bank.c src/rodeo.c src/bob_identity.c -lm -o $@

build/host/editor-test: tests/editor_test.c src/editor.inc src/editor_gen.h src/swing.h src/panel.h
	mkdir -p build/host
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Isrc tests/editor_test.c -o $@

test: verify build/host/dance-hall-test build/host/editor-test build/host/master-show-test build/host/flash-id-test build/host/animation-bank-test
	build/host/dance-hall-test
	mkdir -p build/host
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-unused-function -O2 tests/loader_test.c -o build/host/loader-test
	build/host/loader-test "$(BASE_PACKAGE)" "$(OUTPUT)"
	build/host/editor-test
	build/host/master-show-test
	build/host/flash-id-test
	build/host/animation-bank-test "$(ART)" build/host/r37-show.ppm

# Exercise the real host adapter with the same 40 Hz MASTER polling as hardware.
build/host/master-show-test: tests/master_show_test.c $(wildcard src/*.[ch]) build/classic_bank.h build/host/showbill_calls.h
	mkdir -p build/host
	$(CC) -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation -DSW_WITH_CUE=1 -DSW_WITH_ART=1 -DSW_TARGET_SCORE_SMALL=1 -Ibuild/host -Ibuild/target-base-machine -Isrc -Ibuild tests/master_show_test.c src/swing.c src/panel.c src/showbill.c src/display.c src/art_bank.c src/bank.c src/rodeo.c src/bob_identity.c -lm -o $@


build/host/showbill_calls.h: build/call-index.json tools/call-figure-assignments.json tools/showbill_calls.py
	mkdir -p build/host
	python3 tools/showbill_calls.py build/call-index.json tools/call-figure-assignments.json $@

build/host/flash-id-test: tests/flash_id_test.c src/fm1_flash_id.h
	mkdir -p build/host
	$(CC) -std=c11 -Wall -Wextra -Werror -Isrc $< -o $@

build/host/animation-bank-test: tests/animation_bank_test.c $(wildcard src/*.[ch]) build/classic_bank.h build/host/showbill_calls.h
	mkdir -p build/host
	$(CC) -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation -DSW_WITH_CUE=1 -DSW_WITH_ART=1 -DSW_TARGET_SCORE_SMALL=1 -Ibuild/host -Ibuild/target-base-machine -Isrc -Ibuild tests/animation_bank_test.c src/swing.c src/panel.c src/showbill.c src/display.c src/art_bank.c src/bank.c src/rodeo.c src/bob_identity.c -lm -o $@
