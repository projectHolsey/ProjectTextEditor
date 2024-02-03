MAKE := make

Kilo: Kilo.c
	gcc Kilo.c -o Kilo -Wall -Wextra -pedantic -std=c99
