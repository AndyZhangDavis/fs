#!/bin/sh


# http://www.runoob.com/linux/linux-shell-passing-arguments.html

echo "parameter pass example"
echo "the first is the file name : $0"
echo "1st argc: $1"
echo "2nd argc: $2"
echo "3rd argc: $3"


test_fs.x read df fhello  8000
# https://linuxconfig.org/create-a-random-character-text-file-using-linux-shell
# < /dev/urandom tr -dc "3" | head -c4096 >> fhello
