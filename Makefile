CC = clang
LD = mold
BUILD_DIR = build

OUT_DIRS = $(BUILD_DIR) $(BUILD_DIR)/src $(BUILD_DIR)/src/tree $(BUILD_DIR)/tests

src_files = main.c scheduler.c tree/btree.c tree/node.c tree/cell.c cbuf.c
src_obj_files = $(patsubst %.c, $(BUILD_DIR)/%.o, $(patsubst %, src/%, $(src_files)))

test_files = test_btree.c test_btree_node.c test_cbuf.c
test_obj_files = $(patsubst %.c, $(BUILD_DIR)/%.o, $(patsubst %, tests/%, $(test_files)))
test_targets = $(patsubst %.c, $(BUILD_DIR)/%.t, $(patsubst %, tests/%, $(test_files)))

INCLUDES = -I$(BUILD_DIR)/include/liburing/ -I$(BUILD_DIR)/include/ -I src/include/
CFLAGS = -std=c23 -O3 -Wall -Wextra -march=native -ffunction-sections -Wno-gnu-statement-expression -Wno-zero-length-array -flto $(INCLUDES) -include src/configure.h
LDFLAGS = -flto -fuse-ld=$(LD)
LOADLIBES = -L$(BUILD_DIR)/lib
LIBURING_CFLAGS = -flto -std=c23 -march=native -Wno-zero-length-array -Wno-gnu-statement-expression -Wno-gnu-pointer-arith 

# deps file
override CFLAGS += -MT "$@" -MMD -MP -MF "$@.d"

liburing = $(BUILD_DIR)/lib/liburing.a
bin_scheduler = $(BUILD_DIR)/ioscheduler
configure_output = liburing/config-host.mak
configure_file = liburing/configure

all: build_scheduler tests directories

directories: $(OUT_DIRS)

${OUT_DIRS}:
	@mkdir -p $(OUT_DIRS)

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

$(BUILD_DIR)/%.t: %.c $(liburing) $(src_obj_files)
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $@ $^
	@echo $@

build_scheduler: directories $(bin_scheduler)

tests: directories $(test_targets)

run_tests: tests
	@$(BUILD_DIR)/tests/test_cbuf.t
	@$(BUILD_DIR)/tests/test_btree_node.t
	@$(BUILD_DIR)/tests/test_btree.t

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