#!/usr/bin/perl
use lib do {__FILE__ =~ /^(.*)[\/\\]/; ($1||'.')};
use MakePl;
use Cwd qw(realpath);

$ENV{CC} //= 'gcc';

 # Sample rules

my @objects = qw(events midi_files patch_files player);
my @includes = qw();

sub cc_rule {
    my ($to, $from) = @_;
    rule $to, $from, sub {
        run $ENV{CC}, $from, qw(-c -std=c99 -Wall -ggdb -o), $to;
    };
}
sub ld_rule {
    my ($to, $from) = @_;
    rule $to, $from, sub {
        run $ENV{CC}, @$from, qw(-lSDL2 -lm -o), $to;
    };
}

for (@objects) {
    cc_rule "$_.o", "$_.c";
}
cc_rule 'main_sdl.o', 'main_sdl.c';
cc_rule 'main_profile.o', 'main_profile.c';
ld_rule 'midieval', ['main_sdl.o', map "$_.o", @objects];
ld_rule 'midieval_profile', ['main_profile.o', map "$_.o", @objects];

rule 'clean', [], sub { unlink 'midieval', 'midieval_profile', glob '*.o'; };

defaults 'midieval', 'midieval_profile';

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
