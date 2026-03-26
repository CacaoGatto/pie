CC := g++
CFLAGS := -O2 -fPIC -march=native -Wall -std=gnu++2a
DEFINES :=

PIE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
INC_DIR := $(PIE_DIR)include/
SRC_DIR := $(PIE_DIR)src/
TEST_DIR := $(PIE_DIR)test/

BUILD_DIR := $(PIE_DIR)build/

SRC_FILE := $(wildcard $(SRC_DIR)*.cc)
SRC_OBJ := $(SRC_FILE:$(SRC_DIR)%.cc=$(SRC_DIR)%.o)

TEST_FILE := $(wildcard $(TEST_DIR)*.cc)
TEST_APP := $(TEST_FILE:$(TEST_DIR)%.cc=$(BUILD_DIR)test/%)

.INTERMEDIATE: $(SRC_OBJ)
.PHONY: pie test clean

pie: $(SRC_OBJ)
	@mkdir -p $(BUILD_DIR)
	ar rcs $(BUILD_DIR)libpie.a $^
	$(CC) $(CFLAGS) -shared $^ -o $(BUILD_DIR)libpie.so

$(SRC_DIR)%.o: $(SRC_DIR)%.cc
	$(CC) $(CFLAGS) $(DEFINES) -I$(INC_DIR) -c $< -o $@

test: pie $(TEST_APP)

$(BUILD_DIR)test/%: $(TEST_DIR)%.cc
	@mkdir -p $(BUILD_DIR)test/
	$(CC) $(CFLAGS) $(DEFINES) -I$(INC_DIR) $< -L$(BUILD_DIR) -l:libpie.a -lpthread -o $@

clean:
	rm -rf $(BUILD_DIR)
