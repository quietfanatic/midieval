#!/usr/bin/perl
use lib do {__FILE__ =~ /^(.*)[\/\\]/; ($1||'.')};
use MakePl;

 # Sample rules
rule 'midival', 'midival.c', sub {
    run "gcc -std=c99 -Wall -ggdb midival.c -lm -lSDL2 -o midival";
};
rule 'clean', [], sub { unlink 'midival'; };

make;
