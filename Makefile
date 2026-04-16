CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g
OBJ = main.o shell.o parser.o builtins.o util.o

mysh: $(OBJ)
	$(CC) $(CFLAGS) -o mysh $(OBJ)

main.o: main.c shell.h
shell.o: shell.c shell.h
parser.o: parser.c shell.h
builtins.o: builtins.c shell.h
util.o: util.c shell.h

clean:
	rm -f mysh $(OBJ)

.PHONY: clean
