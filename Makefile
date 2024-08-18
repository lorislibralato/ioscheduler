CC = clang
BUILD_DIR = build

src_files = main.c
obj_files = $(patsubst %.c, $(BUILD_DIR)/%.o,$(src_files)) 
INCLUDES = -I$(BUILD_DIR)/include/liburing/ -I$(BUILD_DIR)/include/
CFLAGS = -O3 -Wall -Wextra -march=native -ffunction-sections -flto $(INCLUDES)
LDFLAGS = -flto -fuse-ld=gold
LOADLIBES = -L$(BUILD_DIR)/lib

liburing = $(BUILD_DIR)/lib/liburing.a
obj = $(BUILD_DIR)/src/client_main.o

bin = $(BUILD_DIR)/client

.PHONY: build clean

dir:
	@mkdir -p $(BUILD_DIR)/src

$(liburing):
	@$(MAKE) -j4 -C liburing install prefix=$(BUILD_DIR) CC=clang LIBURING_CFLAGS="-flto"

$(BUILD_DIR)/%.o: %.c
	@$(CC) $(CFLAGS) -c -o $@ $<

$(bin): $(obj_files) $(liburing) $(obj)
	@$(CC) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^ 


build: dir $(liburing) $(bin)

clean_liburing:
	@$(MAKE) -C liburing clean prefix=$(BUILD_DIR)

clean: clean_liburing
	@rm -rf $(BUILD_DIR)
	@rm -rf $(main)