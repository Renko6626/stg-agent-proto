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

build/test_onnx: build tests/test_onnx.c sa_onnx.c sa_model.c sa_onnx.h sa_model.h vendor/onnxruntime_c_api.h
	$(CC) $(CFLAGS) -o $@ tests/test_onnx.c sa_onnx.c sa_model.c -lm -ldl

test-host: build/test_encode build/test_log build/write_sample_log build/test_model build/test_onnx
	./build/test_encode
	./build/test_log
	./build/test_model
	./build/test_onnx

# 接真 ORT 跑一遍（宿主机器上不强求装 ORT，所以不进 test-host）。用法：
#   make test-onnx-live SA_ONNX_TEST_LIB=/path/libonnxruntime.so SA_ONNX_TEST_MODEL=/path/f-best.onnx \
#                       [SA_ONNX_TEST_BADMODEL=/path/wrong-signature.onnx]
# libonnxruntime.so 可以直接用 pip 装的 onnxruntime 包里那个（site-packages/onnxruntime/capi/）。
test-onnx-live: build/test_onnx
	SA_ONNX_TEST_LIB=$(SA_ONNX_TEST_LIB) SA_ONNX_TEST_MODEL=$(SA_ONNX_TEST_MODEL) \
	SA_ONNX_TEST_BADMODEL=$(SA_ONNX_TEST_BADMODEL) ./build/test_onnx

# 只验「能在 Windows 目标上编译」。x64 给 th06nc，x86 给 th18。XCLANG / XWIN 由调用方按机器覆盖。
cross-%: build
	$(XCLANG) --target=$*-pc-windows-msvc -std=gnu99 -O2 -Wall -Werror -D_CRT_SECURE_NO_WARNINGS -c \
	  -isystem $(XWIN)/crt/include -isystem $(XWIN)/sdk/include/ucrt \
	  -isystem $(XWIN)/sdk/include/um -isystem $(XWIN)/sdk/include/shared \
	  $(SRC) && mv *.o build/ && echo "cross $* ok"

cross-x64: ; $(MAKE) cross-x86_64
cross-x86: ; $(MAKE) cross-i686

# sa_onnx.c 单独一档：它 include vendor 的 ORT 头，且只在 Windows 目标上才走 LoadLibraryW 那条分支。
cross-onnx-%: build
	$(XCLANG) --target=$*-pc-windows-msvc -std=gnu99 -O2 -Wall -Werror -D_CRT_SECURE_NO_WARNINGS -c \
	  -isystem $(XWIN)/crt/include -isystem $(XWIN)/sdk/include/ucrt \
	  -isystem $(XWIN)/sdk/include/um -isystem $(XWIN)/sdk/include/shared \
	  sa_onnx.c && mv sa_onnx.o build/ && echo "cross-onnx $* ok"

.PHONY: test-host test-onnx-live cross-x64 cross-x86
