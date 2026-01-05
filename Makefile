# Compiler
CXX      := g++
AS       := as
CXXFLAGS := -std=c++23 -Wall -Wextra -O3 -pthread -Iinclude
ASFLAGS  := 

# Detect operating system for specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS specific flags if needed
    # Mach APIs are usually linked by default, but be explicit if needed
endif

# Directories
SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := .

# Targets
TARGET   := $(BIN_DIR)/hyperion

# Recursively find all .cpp files in src/
SRCS     := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS_CPP := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# Assembly Source
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    ARCH_SRC := src/kernel/arch/x86_64/switch.S
else
    ARCH_SRC := src/kernel/arch/arm64/switch.S
endif

ARCH_OBJ := $(OBJ_DIR)/kernel/arch/switch.o
OBJS     := $(OBJS_CPP) $(ARCH_OBJ)

# Rules
all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo "Linking $@"
	@$(CXX) $(CXXFLAGS) -o $@ $^

# Pattern rule for C++ object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling C++ $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule for Assembly
$(ARCH_OBJ): $(ARCH_SRC)
	@mkdir -p $(dir $@)
	@echo "Compiling ASM $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

clean:
	@echo "Cleaning..."
	@rm -rf $(OBJ_DIR) $(TARGET) *.db *.wal

.PHONY: all clean
