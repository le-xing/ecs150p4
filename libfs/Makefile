targets := libfs.a
objects := \
	disk.o \
	fs.o   \

CC := gcc
CFLAGS := -Wall -Werror
CFLAGS += -g -O0

all: $(targets)

deps := $(patsubst %.o, %.d, $(objects))
-include $(deps)
DEPFLAGS = -MMD -MF $(@: .o=.d)

libfs.a: $(objects)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(DEPFLAGS)

clean:
	rm -f $(targets) $(objects) $(deps)
