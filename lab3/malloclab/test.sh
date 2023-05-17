MALLOCPATH=/home/ubuntu/oslab/lab3/malloclab/
export LD_LIBRARY_PATH=$MALLOCPATH:$LD_LIBRARY_PATH
echo -e "\033[4;36m* First-fit\033[0m"
./workload
echo -e "\033[4;36m* Best-fit\033[0m"
./workload_best