CC = clang
LD = mold
BUILD_DIR = build

src_files = src/main.c src/scheduler.c
obj_files = $(patsubst %.c, $(BUILD_DIR)/%.o,$(src_files))
INCLUDES = -I$(BUILD_DIR)/include/liburing/ -I$(BUILD_DIR)/include/
CFLAGS = -O3 -Wall -Wextra -march=native -ffunction-sections -flto $(INCLUDES) -include src/configure.h
LDFLAGS = -flto -fuse-ld=$(LD)
LOADLIBES = -L$(BUILD_DIR)/lib

# deps file
override CFLAGS += -MT "$@" -MMD -MP -MF "$@.d"

liburing = $(BUILD_DIR)/lib/liburing.a
bin_scheduler = $(BUILD_DIR)/ioscheduler
bin_legacy = $(BUILD_DIR)/legacy
configure_output = liburing/config-host.mak
configure_file = liburing/configure

all: build_scheduler build_legacy

dir:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/src

$(configure_output): $(configure_file)
	+@cd liburing && \
	if [ ! -e "$@" ]; then	\
	  # echo "Running configure ...";	\
	  ./configure --prefix=../build --cc=$(CC) --cxx=$(CC) --use-libc > /dev/null; \
	else	\
	  # echo "$@ is out-of-date, running configure";	\
	  sed -n "/.*Configured with/s/[^:]*: //p" "$@" | sh > /dev/null;	\
	fi

$(liburing): liburing/config-host.mak
	@$(MAKE) -C liburing/src install ENABLE_SHARED=0 MAKEFLAGS="s" CC=$(CC) LIBURING_CFLAGS="-flto"
	@echo $@

-include $(obj_files:%=%.d)
$(BUILD_DIR)/%.o: %.c
	@$(CC) $(CFLAGS) -c -o $@ $<
	@echo $@

$(bin_scheduler): $(liburing) $(obj_files)
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^
	@echo $@

-include $(BUILD_DIR)/src/main_legacy.o.d
$(bin_legacy): $(BUILD_DIR)/src/main_legacy.o
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^
	@echo $@

build_scheduler: dir $(bin_scheduler)

build_legacy: dir $(bin_legacy)

clean_liburing:
	@rm -rf liburing/config-host.mak
	@$(MAKE) -C liburing/src clean

clean: clean_liburing
	@rm -rf $(BUILD_DIR)