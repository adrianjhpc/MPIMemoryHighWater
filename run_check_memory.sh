#!/bin/bash
#SBATCH -D /my/home/directory/
#SBATCH -o /my/home/directory/myapplication.%A.out
#SBATCH -e /my/home/directory/myapplication.%A.err
#SBATCH -J myapplication
#SBATCH --nodes=1
#SBATCH --tasks-per-node=36
#SBATCH --time=1:00:0
	
mpirun -n 1 ./checkmemory.sh &
pids=$!
mpirun -n 36 ./myapplication
kill -9 $pids

