#! /bin/bash

TASKPATH=$PWD
MALLOCPATH=/home/ubuntu/USTC-OSLabs/lab3/malloclab/ # 需要修改为你的libmem.so所在目录
export LD_LIBRARY_PATH=$MALLOCPATH:$LD_LIBRARY_PATH
cd $MALLOCPATH; make clean; make
cd $TASKPATH
g++ workload.cc -o workload -I$MALLOCPATH -L$MALLOCPATH -lmem -lpthread # -std=c++11
# ./workload