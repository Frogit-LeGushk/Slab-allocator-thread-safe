

build: main.cpp
	g++ main.cpp -Wall -Werror -Wextra -o main -std=gnu++17 -pthread

run: 
	./main
