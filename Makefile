CC ?= gcc
CFLAGS :=								\
	-std=gnu2x							\
	-Wall								\
	-Wextra								\
	-Werror								\
	-ggdb								\
	-fsanitize=undefined				\
	-fsanitize=address					\
	`pkg-config --cflags guile-2.2`		\

LDFLAGS += 							\
    -fsanitize=undefined 			\
    -fsanitize=address				\
	`pkg-config --libs guile-2.2`	\
	-lm

TARGET = lambuild
BUILD_DIRECTORY = build
DIRECTORY_GUARD = @mkdir -p $(@D)
SRC = $(wildcard src/*.c)
OBJ = $(patsubst %.c, $(BUILD_DIRECTORY)/%.c.o, $(SRC))

$(BUILD_DIRECTORY)/%.c.o: %.c
	@$(DIRECTORY_GUARD)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	@$(DIRECTORY_GUARD)
	@echo "[LD] $@"
	@$(CC) $(LDFLAGS) -o $@ $^

all: $(TARGET)

clean: 
	-rm -r $(BUILD_DIRECTORY)

distclean: clean
	-rm $(TARGET)

.PHONY: all clean distclean
.DEFAULT: all