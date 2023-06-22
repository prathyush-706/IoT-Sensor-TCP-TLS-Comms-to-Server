# Prathyush Sivakumar
# prathyush1999@ucla.edu
# 704908810

default: lab4c

lab4c: lab4c_tcp.c lab4c_tls.c
	gcc -Wall -Wextra -g -lmraa -lm -o lab4c_tcp lab4c_tcp.c
	gcc -Wall -Wextra -g -lmraa -lm -lssl -lcrypto -o lab4c_tls lab4c_tls.c

clean:
	rm -rf *.o *.tar.gz *.dSYM lab4c_tcp lab4c_tls

dist:
	tar -czvf lab4c-704908810.tar.gz lab4c_tls.c lab4c_tcp.c Makefile README
