EXE := killo
OBJ := killo.o
SRC := killo.c
CFLAGS := -Wall -Wextra -Weffc++ -Wsign-conversion -pedantic-errors -std=c99

.PHONY: all clean debug

all: $(EXE)

debug: CFLAGS += -g3
debug: $(EXE)

$(EXE): $(SRC)
	$(CC) $(SRC) -o $(EXE) $(CFLAGS)

clean:
	$(RM) -rf $(OBJ) $(EXE) killo.dSYM

