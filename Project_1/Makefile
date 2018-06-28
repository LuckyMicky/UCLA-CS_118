CC = gcc
CFLAGS = -g -Wall
UID = 204853061
TARBALL = $(UID).tar.gz
SOURCE = webserver.c  README  Makefile report.pdf

default:
	@$(CC) $(CFLAGS) -o webserver webserver.c
clean:
	@rm -f webserver *.tar.gz 
dist: default
	tar -czf $(TARBALL) $(SOURCE)
