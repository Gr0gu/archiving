#include <stdio.h>
#include <stdlib.h>

void heapify(int *a){
	

}


void compressFile(char *filename){
	FILE *file = fopen(filename, "r");
	int f[256] = {0};
	char ch = ' ';
	while(ch != EOF){
		ch = fgetc(file);
		f[(int)ch]++;		
	}


	for(int i = 0 ; i < 256 ; ++i)
		printf("%d: %d\n", i,  f[i]);
}


int main(){
	compressFile("input.txt");

	return 0;
}
