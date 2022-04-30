#1 mbr
nasm -I include/ -o mbr.bin mbr.S
# mbr.bin 写进 hd60M.img 的第 0 块了
# bs: 块的大小(单位B)  count:块的个数  conv=notrunc:追加数据时不打断文件  seek=1跳过多少块
dd if=mbr.bin of=/usr/local/bochs/hd60M.img bs=512 count=1 conv=notrunc

#2 loader
nasm -I include/ -o loader.bin loader.S
dd if=loader.bin of=/usr/local/bochs/hd60M.img bs=512 count=4 seek=2 conv=notrunc

#3
# 编译print
nasm -f elf -o build/print.o lib/kernel/print.S
nasm -f elf -o build/kernel.o kernel/kernel.S
# 编译main
gcc -I lib/ -I kernel/ -m32 -c -fno-builtin -o build/timer.o device/timer.c
gcc -I lib/ -I kernel/ -m32 -c -fno-builtin -o build/main.o kernel/main.c
gcc -I lib/ -I kernel/ -m32 -c -fno-builtin -o build/interrupt.o kernel/interrupt.c 
gcc -I lib/ -I kernel/ -m32 -c -fno-builtin -o build/init.o kernel/init.c
# 链接print和main
ld -m elf_i386 build/main.o build/init.o build/interrupt.o build/print.o build/kernel.o build/timer.o -Ttext 0xc0001500 -e main -o build/kernel.bin
dd if=build/kernel.bin of=/usr/local/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc