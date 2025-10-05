
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

STATIC_LIB := $(BUILD_DIR)/libcpmfs.a
DYN_LIB := $(BUILD_DIR)/libcpmfs.so

SRC := src/cpmfs.c src/cpmfs_utils.c src/cpmfs_check.c

OBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRC))

all: libcpmfs

libcpmfs: $(STATIC_LIB) $(DYN_LIB)

$(STATIC_LIB): $(OBJECTS)
	@echo "AR" $@
	@$(AR) rcs $@ $^

$(DYN_LIB): $(OBJECTS)
	@echo "CC" $@
	@$(CC) -shared -rdynamic -o $@ $^

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@echo "CC" $^
	@$(CC) -c $^ -o $@ -I include/

$(OBJ_DIR): | $(BUILD_DIR)
	mkdir $@

$(BUILD_DIR):
	mkdir $@

clean:
	rm -f $(STATIC_LIB) $(DYN_LIB)
	rm -f $(OBJECTS)
	rm -df $(OBJ_DIR)
	rm -df $(BUILD_DIR)

.PHONY: libcpmfs
