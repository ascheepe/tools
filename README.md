# Fit and Shuffle

## Fit
Fit is used to easily copy files to fixed size storage. You can see
how many disks it will take (-n) to store the given path, or show
(default) the contents of each disk and finally link (-l) the disks to
numbered directories on the same partition so you can easily copy it.

## Shuffle
Shuffle is used to run a program for each of the files with match
a given extension or filetype in random order. This is a builtin in
many media players but for some formats it comes in handy (I use it
to play sid music with sidplay).

NOTE: This depends on libmagic being available to be able to select
files by type.

## mvd
With mvd you can move files into directories named after their
modification time. Usage is like mv except the target directory
must not cross filesystems. You can specify a strftime date format
with the `-f` parameter.

Example: `mvd * .` will move every file in the current directory into
directories name like YYYYmm. I use this to clean up the Downloads folder.

