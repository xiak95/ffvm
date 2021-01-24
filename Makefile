all:
	gcc ffvm/riscv.c -o ffvm_sim
clean:
	rm ffvm_sim