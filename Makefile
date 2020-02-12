
pCp : pCp.c
	gcc -o pCp pCp.c -lpthread

clean : 
	rm *.o pCp
