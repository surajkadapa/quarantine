# Makefile for Sandbox Project

CC = gcc
CFLAGS = -Wall -O2 -g

SRCS = sandbox.c cgroups.c

OBJS = $(SRCS:.c=.o)

TARGET = sandbox

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Build complete: $@"

%.o: %.c sandbox.h cgroups.h
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up"
	rm -f $(OBJS) $(TARGET)
	@echo "Clean complete"

rebuild: clean all

.PHONY: all clean rebuild run