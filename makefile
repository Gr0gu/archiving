# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g

# The final executable name (as defined in your README)
TARGET = sudo_archiver

# All source files that make up the project
SRCS = compresser.c arc.c bwt.c MTF.c rle.c lz77.c huffman.c

# Automatically generate a list of object files (.o) from the source files (.c)
OBJS = $(SRCS:.c=.o)

# Default target when you just type 'make'
.PHONY: all clean

all: $(TARGET)

# Linking the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Build successful! Run with ./${TARGET}"

# Compiling individual C files into object files
# Every .o file depends on its .c file and pipeline.h
%.o: %.c pipeline.h
	$(CC) $(CFLAGS) -c $< -o $@

# Cleanup target to remove compiled files
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "Cleaned up build files."