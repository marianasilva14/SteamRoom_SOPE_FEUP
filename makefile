saunamake: sauna.c gerador.c
	gcc sauna.c -lpthread -lrt -Wall -o sauna
	gcc gerador.c -lpthread -Wall -o gerador
