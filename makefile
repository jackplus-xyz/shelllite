CC = gcc -std=c99
FLAG = -Werror=vla
EXE = smallsh
.PHONY : clean

$(EXE) : $(EXE).c
	$(CC) $(FLAG) -o $(EXE) $^

clean:
	@find . -type f -name '*.o' -exec rm -f {} 2> /dev/null \;
	@find . -type f -perm /u+x -exec rm -f {} 2> /dev/null \;