#include <stdio.h>
#include <stdlib.h>

void win() {
	printf("You have successfully overflown the buffer!\n");
}

void vulnerable() {
	char buffer[16];

	printf("Enter your name: ");
	gets(buffer);

	printf("Hello, %s\n", buffer);
}

int main(int argc, char* argv[]) {
	vulnerable();
	return 0;
}
