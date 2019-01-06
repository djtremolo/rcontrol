

make clean

#scp dev_rcontrol.c ht@djembe:~/rcontrol/dev_rcontrol
#scp dev_rcontrol.h ht@djembe:~/rcontrol/dev_rcontrol
#scp Makefile ht@djembe:~/rcontrol/dev_rcontrol

make


sudo mknod /dev/dev_rcontrol c 222 0

sudo rmmod dev_rcontrol.ko
sudo insmod dev_rcontrol.ko

read -p "press enter to overwrite dev_rcontrol.ko at /etc"


sudo cp dev_rcontrol.ko /etc/



