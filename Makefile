CC = gcc
CFLAGS = -Wall -Wextra -pthread

SRC = src/main.c src/config.c src/interfaces.c src/routing.c src/network.c
OBJ = $(SRC:.c=.o)
TARGET = rip_router

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

clean:
	rm -f src/*.o $(TARGET)