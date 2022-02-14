CC ?= gcc
CFLAGS :=								\
	-std=gnu2x							\
	-Wall								\
	-Wextra								\
	-Werror								\
	-ggdb								\
	`pkg-config --cflags guile-2.2`		

LDFLAGS += 							\
	-lpthread						\
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

install: $(TARGET)
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f lambuild ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/lambuild

clean: 
	-rm -r $(BUILD_DIRECTORY)

distclean: clean
	-rm $(TARGET)

.PHONY: all clean distclean
.DEFAULT: all
