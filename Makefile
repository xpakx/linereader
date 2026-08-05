.PHONY: run build

build: main

run: main
	./main

main: main.c
	gcc main.c -o main
