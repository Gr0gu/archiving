CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2
TARGET  = arc

all: $(TARGET)

$(TARGET): arc.c arc.h
	$(CC) $(CFLAGS) -o $@ arc.c

clean:
	rm -f $(TARGET) *.arc test_*

.PHONY: all clean
