#!/bin/bash

gcc -O3 compresser.c bwt.c MTF.c rle.c huffman.c arc.c -lpthread -o sudo_archiver
