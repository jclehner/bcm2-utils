CC ?= cc
CFLAGS ?= -Wall -g

bcm2cfg: bcm2cfg.c
	$(CC) $(CFLAGS) bcm2cfg.c -o bcm2cfg -lssl -lcrypto

clean:
	rm -f bcm2cfg
