all:
	gcc -o ipc-c ./ipc-c.c

run:
	./ipc-c

clean:
	rm -f ipc-c
