CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
TARGET = memory_benchmark
OBJS = main.o memory_allocation.o

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

main.o: main.c memory_allocation.h
	$(CC) $(CFLAGS) -c main.c

memory_allocation.o: memory_allocation.c memory_allocation.h
	$(CC) $(CFLAGS) -c memory_allocation.c

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
