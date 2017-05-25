saunamake: sauna.c gerador.c
	gcc sauna.c -g -lpthread -lrt -Wall -o sauna
	gcc gerador.c -g -lpthread -Wall -o gerador
clean:
	$(RM) gerador sauna
