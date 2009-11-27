#!/bin/sh

cd ~

for i in `ls`
do
    cd $i

    for i in {0..100}
    do
        while [ 1 ]
        do
            # Name of files is randomly generated to avoid overwrite on
            # multiple executions of the script...
            RSTR=$( head -c 10 /dev/urandom | uuencode -m - | head -n 3 | tail -n 2 | tr "\n" "x" | tr "/" "s" | tr "+" "p" | head -c 10 )
            RSTR=this_is_a_test_$RSTR

            # ... but an extra check is performed
            if [ ! -e $RSTR ]
            then
                break
            fi
        done

        touch $RSTR
    done

    if [ `ls -1 this_is_a_test_* | wc -l` -ne 100 ]
    then
        echo Failed to create 100 files in `pwd`
        exit 1
    fi

    cd -
done

exit 0
