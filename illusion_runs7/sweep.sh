#!/bin/sh

#resnet50_16_1_128_torus_1_1

for wl in alex_net_16_1 resnet50_16_1 vgg_net_16_1
do
    for inj in 0.05 0.1 0.15 0.2 0.25 0.3 0.35 0.4
    do
        for cfg in 0.25 0.5 1 2 4 8 16 32 64 128 256 512 1024 2048
        do
            configs=`ls ../illusion_configs/$wl\_$cfg\_$network*`
            for f in $configs
            do
                my_arr=($(echo $f | tr "\/" "\n"))
                #echo ${my_arr[2]}   
                ../src/booksim $f injection_rate=$inj stats_out=stats_${my_arr[2]}_$inj\.m power_output_file=power_${my_arr[2]}_$ing\.txt > results_${my_arr[2]}_$inj\.txt &
            done 
        done
    done
done

