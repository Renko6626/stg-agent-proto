CC      ?= gcc
CFLAGS  ?= -std=gnu99 -O2 -Wall -Wextra -Werror
XWIN    ?= $(HOME)/.cache/cargo-xwin/xwin
XCLANG  ?= clang
SRC     := sa_encode.c sa_log.c sa_model.c

build:
	mkdir -p build

build/test_encode: build tests/test_encode.c sa_encode.c stgagent.h sa_layout.h world.h
	$(CC) $(CFLAGS) -o $@ tests/test_encode.c sa_encode.c -lm

build/test_log: build tests/test_log.c $(SRC) stgagent.h
	$(CC) $(CFLAGS) -o $@ tests/test_log.c $(SRC) -lm

build/write_sample_log: build tests/write_sample_log.c $(SRC) stgagent.h
	$(CC) $(CFLAGS) -o $@ tests/write_sample_log.c $(SRC) -lm

build/test_model: build tests/test_model.c sa_model.c sa_model.h world.h
	$(CC) $(CFLAGS) -o $@ tests/test_model.c sa_model.c -lm

test-host: build/test_encode build/test_log build/write_sample_log build/test_model
	./build/test_encode
	./build/test_log
	./build/test_model

# 只验「能在 Windows 目标上编译」。x64 给 th06nc，x86 给 th18。XCLANG / XWIN 由调用方按机器覆盖。
cross-%: build
	$(XCLANG) --target=$*-pc-windows-msvc -std=gnu99 -O2 -Wall -Werror -D_CRT_SECURE_NO_WARNINGS -c \
	  -isystem $(XWIN)/crt/include -isystem $(XWIN)/sdk/include/ucrt \
	  -isystem $(XWIN)/sdk/include/um -isystem $(XWIN)/sdk/include/shared \
	  $(SRC) && mv *.o build/ && echo "cross $* ok"

cross-x64: ; $(MAKE) cross-x86_64
cross-x86: ; $(MAKE) cross-i686

.PHONY: test-host cross-x64 cross-x86
