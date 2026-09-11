use warnings;
use strict;
use autodie qw(:all);

use File::Find;
use Getopt::Long;
use List::Util qw(shuffle);

sub main {
    my $path = ".";
    my $extension;

    GetOptions(
        "extension=s" => \$extension,
        "path=s"      => \$path,
    ) or die("Error in command line arguments\n");

    if ( defined($extension) && $extension !~ /^\./ ) {
        $extension = lc(".$extension");
    }

    my @files;
    File::Find::find(
        sub {
            return if !-f;
            if ( defined($extension) ) {
                return if ( lc($_) !~ /$extension$/ );
            }
            push( @files, $File::Find::name );
        },
        $path
    );

    @files = shuffle @files;

    foreach my $file (@files) {
        my @command = @ARGV;
        my $has_placeholder = grep { /\{\}/ } @command;

        if ($has_placeholder) {
            map { s/\{\}/$file/ } @command;
        }
        else {
            push( @command, $file );
        }

        print("==> @command\n");
        system(@command);
    }
}

main();
