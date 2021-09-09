#include <stdio.h>

int main() {
	int i = 0;
	int a[100];

	for (i=0; i<sizeof(a); i++) {
	  a[i] = i;
	}
	printf("GOGO\n");
	return 0;
}
