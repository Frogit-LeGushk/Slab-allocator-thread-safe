CC=g++
CGLAGS=-Wall -Werror -Wextra -ggdb -g -g3 -std=gnu++17
LFLAGS=-pthread

run_debug_thread: build_debug_thread 
	./main
run_debug_adress: build_debug_adress
	./main

build_debug_thread: main.cpp
	$(CC) $(CGLAGS) -fsanitize=thread -o main main.cpp $(LFLAGS)
build_debug_adress: main.cpp
	$(CC) $(CGLAGS) -fsanitize=address -o main main.cpp $(LFLAGS)


