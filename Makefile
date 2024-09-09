CC = clang
BUILD_DIR = build

src_files = src/main.c src/scheduler.c
obj_files = $(patsubst %.c, $(BUILD_DIR)/%.o,$(src_files)) 
INCLUDES = -I$(BUILD_DIR)/include/liburing/ -I$(BUILD_DIR)/include/
CFLAGS = -O3 -Wall -Wextra -march=native -ffunction-sections -flto $(INCLUDES)
LDFLAGS = -flto -fuse-ld=mold
LOADLIBES = -L$(BUILD_DIR)/lib

liburing = $(BUILD_DIR)/lib/liburing.a

bin = $(BUILD_DIR)/main

.PHONY: build clean

dir:
	@mkdir -p $(BUILD_DIR)/src
	@mkdir -p $(BUILD_DIR)/lib	

$(liburing):
	@$(MAKE) -j4 -C liburing install prefix=$(BUILD_DIR) CC=clang LIBURING_CFLAGS="-flto"

$(BUILD_DIR)/%.o: %.c
	@$(CC) $(CFLAGS) -c -o $@ $<

$(bin): $(obj_files) $(liburing)
	@$(CC) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^ 


# build liburing first
build: dir $(liburing) $(bin)

clean_liburing:
	@$(MAKE) -C liburing clean prefix=$(BUILD_DIR)

clean: clean_liburing
	@rm -rf $(BUILD_DIR)
