CC = gcc
CFLAGS = -Wall -Wextra

SRC = src/main.c src/network.c src/routing.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)
TARGET = rip-in-c

#target predefinito chiamando solo make. richiede la compilazione di $(TARGET)
all: $(TARGET)

# fase di linking. se i file .o sono pronti unisce tutti gli oggetti in un unico eseguibile di nome TARGET
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

clean:
	rm -f src/*.o src/*.d $(TARGET)