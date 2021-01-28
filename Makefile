all:
	gcc ffvm/riscv.c -I/usr/include/SDL2 -D_REENTRANT -lSDL2 -o ffvm_sim 

clean:
	rm ffvm_sim