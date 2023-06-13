#!/bin/bash
PS4='> $ '
# set -x

# cd correct directory
cd "$(dirname "$0")"

# generate image
rm -f ./fat16-test-32M.img
mkfs.fat -C -F 16 -r 512 -R 32 -s 4 -S 512 ./fat16-test-32M.img $((32*1024))

# mount image
sudo umount ./vfat
rm -rf ./vfat
mkdir -p ./vfat
sudo mount -t vfat \
    --options "time_offset=480,iocharset=ascii,uid=$(id -u ${USER}),gid=$(id -g ${USER})" \
    ./fat16-test-32M.img ./vfat

# copy files into image
python3 ./generate_test_files.py
cp -r ./_test_files/* ./vfat/
sudo umount ./vfat

rm -rf ./fat16
mkdir -p ./fat16

cp ./fat16-test-32M.img ./fat16-tmp.img
fusermount -zu ./fat16
./std/simple_fat16 -s ./fat16 --img="./fat16-test-32M.img" --seek_time=10
python3 ./fat16_bench.py ./fat16 | tee /tmp/std_time.txt
fusermount -zu ./fat16

cp ./fat16-tmp.img ./fat16-test-32M.img
fusermount -zu ./fat16
make -C .. debug
../simple_fat16 -s ./fat16 --img="./fat16-test-32M.img" --seek_time=10
python3 ./fat16_bench.py ./fat16 | tee /tmp/your_time.txt
fusermount -zu ./fat16

rm ./fat16-tmp.img

echo "std time:"
tail -n 1 /tmp/std_time.txt
echo "your time:"
tail -n 1 /tmp/your_time.txt