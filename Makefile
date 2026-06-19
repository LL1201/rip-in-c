CC = gcc
CFLAGS = -Wall -Wextra -pthread

SRC = src/main.c src/network.c src/routing.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)
TARGET = rip-in-c

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

clean:
	rm -f src/*.o src/*.d $(TARGET)

.PHONY: all clean