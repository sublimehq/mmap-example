CC = g++

read_mmap: read_mmap.cc
	$(CC) -Wall -O3 -o read_mmap read_mmap.cc
