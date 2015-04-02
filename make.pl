#!/usr/bin/perl
use lib do {__FILE__ =~ /^(.*)[\/\\]/; ($1||'.')};
use MakePl;

$ENV{CC} //= 'gcc';

 # Sample rules

my @objects = qw(events midi_files patch_files player);
my @includes = qw(inc);

sub cc_rule {
    my ($to, $from) = @_;
    rule $to, $from, sub {
        run $ENV{CC}, $from, map("-I$_", @includes), qw(-c -std=c99 -Wall -ggdb -o), $to;
    };
}
sub ld_rule {
    my ($to, $from) = @_;
    rule $to, $from, sub {
        run $ENV{CC}, @$from, qw(-lSDL2 -lm -o), $to;
    };
}

for (@objects) {
    cc_rule "tmp/$_.o", "src/$_.c";
}
cc_rule 'tmp/main_sdl.o', 'src/main_sdl.c';
cc_rule 'tmp/main_profile.o', 'src/main_profile.c';
ld_rule 'midieval_sdl', ['tmp/main_sdl.o', map "tmp/$_.o", @objects];
ld_rule 'midieval_profile', ['tmp/main_profile.o', map "tmp/$_.o", @objects];

rule 'clean', [], sub { unlink 'midieval_sdl', 'midieval_profile', glob 'tmp/*'; };

defaults 'midieval_sdl', 'midieval_profile';

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
            push @r, rel2abs("$I/$_") if -e("$I/$_");
        }
    }
    return @r;
};

make;
