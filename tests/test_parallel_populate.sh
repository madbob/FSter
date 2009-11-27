#!/bin/sh

cd ~

for i in {0..5}
do
    sh test_easy_populate.sh &

    # A delay is performed between multiple executions to be sure at certain
    # time more instances will write on different Containers
    sleep 2
done
