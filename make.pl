#!/usr/bin/perl
use lib do {__FILE__ =~ /^(.*)[\/\\]/; ($1||'.')};
use MakePl;
use Cwd qw(realpath);

 # Sample rules

my @objects = qw(main midi player);
my @includes = qw();

sub cc_rule {
    my ($to, $from) = @_;
    rule $to, $from, sub {
        run 'gcc', $from, qw(-c -std=c99 -Wall -ggdb -o), $to;
    };
}
sub ld_rule {
    my ($to, $from) = @_;
    rule $to, $from, sub {
        run 'gcc', @$from, qw(-lSDL2 -lm -o), $to;
    };
}

for (@objects) {
    cc_rule "$_.o", "$_.c";
}
ld_rule 'midival', [map "$_.o", @objects];

rule 'clean', [], sub { unlink 'midival', glob '*.o'; };

defaults 'midival';

 # Automatically glean subdeps from #includes
subdep sub {
    my ($file) = @_;
     # Select only C++ files
    $file =~ /\.(?:c(?:pp)?|h)$/ or return ();

    my $base = ($file =~ /(.*?)[^\\\/]*$/ and $1);
    my @incs = (slurp $file, 2048) =~ /^\s*#include\s*"([^"]*)"/gmi;
    my @r;
    for (@incs) {
        for my $I (@includes, $base) {
            push @r, realpath("$I/$_") if -e("$I/$_");
        }
    }
    return @r;
};

make;
