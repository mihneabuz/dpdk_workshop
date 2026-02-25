CC = gcc
OPT = O0

DPDK_INCLUDES =  -I/usr/include/dpdk -I/usr/include/x86_64-linux-gnu/dpdk
DPDK_LIBS = -lrte_eal -lrte_ethdev -lrte_mbuf -lrte_mempool -lrte_ring -lrte_net_ring

FLAGS = $(DPDK_INCLUDES) $(DPDK_LIBS) -$(OPT) -march=native

# BONUS TASK 1: you can provide more lcores by changing the -l flags
#

# BONUS TASK 2: you can add real interfaces here like this:
#               --vdev="net_af_packet0,iface=veth0"
#               --vedv="net_af_packet1,iface=veth1"

RTE_RUN_FLAGS = -l 0-3 -m 512 --no-huge --no-telemetry

build: app

app: src/main.c
	$(CC) $^ -o $@ $(FLAGS)

run: app
	./app $(RTE_RUN_FLAGS)

clean:
	rm -f app src/*.o

devenv:
	docker build -t dpdk-dev-env .

dev:
	docker run -v .:/workshop -w /workshop -it --rm dpdk-dev-env

.PHONY: docker dev
