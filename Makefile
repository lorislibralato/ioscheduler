CC = clang
LD = mold
BUILD_DIR = build

src_files = src/main.c src/scheduler.c src/btree.c src/cbuf.c
src_obj_files = $(patsubst %.c, $(BUILD_DIR)/%.o, $(src_files))

test_files = tests/test_btree_node.c tests/test_cbuf.c
test_obj_files = $(patsubst %.c, $(BUILD_DIR)/%.o, $(test_files))
test_targets = $(patsubst %.c, $(BUILD_DIR)/%.t, $(test_files)) 

INCLUDES = -I$(BUILD_DIR)/include/liburing/ -I$(BUILD_DIR)/include/
CFLAGS = -std=c23 -O3 -Wall -Wextra -march=native -ffunction-sections -Wno-gnu-statement-expression -Wno-zero-length-array -flto $(INCLUDES) -include src/configure.h
LDFLAGS = -flto -fuse-ld=$(LD)
LOADLIBES = -L$(BUILD_DIR)/lib
LIBURING_CFLAGS = -flto -std=c23 -march=native -Wno-zero-length-array -Wno-gnu-statement-expression -Wno-gnu-pointer-arith 

# deps file
override CFLAGS += -MT "$@" -MMD -MP -MF "$@.d"

liburing = $(BUILD_DIR)/lib/liburing.a
bin_scheduler = $(BUILD_DIR)/ioscheduler
bin_legacy = $(BUILD_DIR)/legacy
configure_output = liburing/config-host.mak
configure_file = liburing/configure

all: build_scheduler build_legacy tests

dir:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/src
	@mkdir -p $(BUILD_DIR)/tests

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
	@$(MAKE) -C liburing/src install ENABLE_SHARED=0 MAKEFLAGS="s" CC=$(CC) LIBURING_CFLAGS="$(LIBURING_CFLAGS)"
	@echo $@

-include $(src_obj_files:%=%.d)
$(BUILD_DIR)/%.o: %.c
	@$(CC) $(CFLAGS) -c -o $@ $<
	@echo $@

$(bin_scheduler): $(liburing) $(src_obj_files)
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^
	@echo $@

-include $(BUILD_DIR)/src/main_legacy.o.d
$(bin_legacy): $(BUILD_DIR)/src/main_legacy.o
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^
	@echo $@

$(BUILD_DIR)/%.t: %.c $(src_obj_files)
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^
	@echo $@

build_scheduler: dir $(bin_scheduler)

build_legacy: dir $(bin_legacy)

tests: dir $(test_targets)

perf: build_scheduler
	@rm -rf __test_perf.db
	@perf record -F 10000 --call-graph dwarf $(bin_scheduler) __test_perf.db

flamegraph:
	@perf script | ~/repo/FlameGraph/stackcollapse-perf.pl | ~/repo/FlameGraph/stackcollapse-recursive.pl | ~/repo/FlameGraph/flamegraph.pl > perf_flamegraph.svg

clean_liburing:
	@rm -rf liburing/config-host.mak
	@$(MAKE) -C liburing/src clean

clean: clean_liburing
	@rm -rf $(BUILD_DIR)