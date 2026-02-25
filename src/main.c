#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rte_debug.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip4.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#include "virtual_rings.h"

#define DEBUG 0
#define dprintf(...)                                                           \
  do {                                                                         \
    if (DEBUG)                                                                 \
      printf(__VA_ARGS__);                                                     \
  } while (0)

// BONUS TASK 2: Use real interfaces
//
// Instead of creating virtual interfaces at runtime, we can
// use some interfaces from our host. Unfortunately, you probably
// don't have 2 physical ports connected back-to-back on your laptop :)
//
// Thankfully, we can create some virtual interfaces like so:
//   ip link add veth0 type veth peer name veth1
//
//   ip link set veth0 up
//   ip link set veth1 up
//
// This might not work inside the dev container, as it does not
// have persmission to create virtual interfaces.
//
// In order to give the app access to host interfaces, you can
// edit the RTE_RUN_FLAGS in the Makefile.
//
#define USE_VIRTUAL_RINGS 1

#define RX_PORT 0
#define TX_PORT 1

#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 512
#define NUM_DESC 2048

// TASK 4: Play around with the value for these constants
//
// You can set the BURST_DELAY to 0 to force both loops
// to run as fast as possible.
//
// You can increase BURST_SIZE to increase the amount of
// batching of packets. Make sure to also increase the
// PACKET_COUNT to fully observe the effects.
//
// If you want to test the performance, consider setting
// the DEBUG var to 0 and removing any extra logging you
// added in the RX and TX loops.
//
#define BURST_SIZE 32
#define BURST_DELAY 0

#define PACKET_SIZE 1500
#define PACKET_COUNT 100000000

struct rx_context {
  uint16_t port_id;
  uint16_t burst_size;
  uint16_t burst_delay;

  uint64_t packet_count;
  uint64_t packet_size;
};

struct tx_context {
  uint16_t port_id;
  uint16_t burst_size;
  uint16_t burst_delay;

  uint64_t packet_count;
  uint64_t packet_size;

  uint16_t addr_count;
  uint32_t src_ip_addr_start;
  uint32_t dst_ip_addr_start;

  struct rte_mempool *pool;
};

static int tx_worker(void *arg) {
  struct tx_context *ctx = (struct tx_context *)arg;

  struct rte_mbuf *bufs[ctx->burst_size];

  dprintf("[TX] Starting TX worker for port %d\n", ctx->port_id);

  // TASK 2: Complete the TX loop

  uint64_t tx_count = 0;
  while (tx_count < ctx->packet_count) {
    uint16_t burst_size =
        RTE_MIN(ctx->burst_size, ctx->packet_count - tx_count);

    // TODO 2.1: First we need to allocate some mbufs
    // from the mempool provided in the tx_context.
    //
    // We can use `rte_pktmbuf_alloc` to request a single mbuf,
    // or `rte_pktmbuf_alloc_bulk` to request burst_size mbufs
    //
    if (rte_pktmbuf_alloc_bulk(ctx->pool, bufs, burst_size) < 0)
      rte_panic("[TX] Cannot allocate %d mbufs\n", burst_size);

    for (int i = 0; i < burst_size; i++) {
      // Build the packet
      uint64_t packet_id = tx_count + i;

      // TODO 2.2: Allocate space for the payload.
      //
      // Initially, the mbuf will have a payload capacity of 0.
      // We can extend this using `rte_pktmbuf_append`.
      //
      // The total size of the packet should be the one specified
      // in the tx_config.
      //
      char *pkt = rte_pktmbuf_append(bufs[i], ctx->packet_size);
      if (pkt == NULL)
        rte_panic("[TX] Cannot append to mbuf\n");

      // TODO 2.3: Fill the Ethernet header
      //
      // For the Ethernet header, we mostly care about setting
      // the MAC src and dst addresses and the ether_type.
      //
      // The addresses are not really relevant, as we won't be
      // tracking them, you can set them to any value.
      //
      // The ether_type should be set to IPv4, there is a
      // constant for that: RTE_ETHER_TYPE_IPV4
      //
      // NOTE: when setting values in packet headers, make sure to
      //       use correct byte order! DPDK provides some helpers:
      //       rte_cpu_to_be_32, rte_be_to_cpu_32
      //
      struct rte_ether_hdr *eth = (struct rte_ether_hdr *)pkt;
      memset(eth->dst_addr.addr_bytes, 0xFF, RTE_ETHER_ADDR_LEN);
      memset(eth->src_addr.addr_bytes, 0x02, RTE_ETHER_ADDR_LEN);
      eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

      // TODO 2.4: Fill the IPv4 header
      //
      // For the IPv4 header we need to set more fields.
      //
      // For the src and dst addresses, we should use the value specified
      // in the tx_context. As a bonus, we can increment it by packet_id
      // to give each packet unique addresses.
      //
      // The total_length field must be set to the total length of the
      // packet minus the Ethernet header size.
      //
      // The time_to_live field must be set to a value > 0 to make
      // sure our packet is not dropped by a router. We can use the
      // max value of 64.
      //
      // We should also calculate the header for the checksum. This is done
      // by first setting the hdr_checksum to 0 then calculating the final
      // value using the function rte_ipv4_cksum.
      //
      // NOTE: when setting values in packet headers, make sure to
      //       use correct byte order! DPDK provides some helpers:
      //       rte_cpu_to_be_32, rte_be_to_cpu_32
      //
      struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
      ip->src_addr = rte_cpu_to_be_32(ctx->src_ip_addr_start + packet_id);
      ip->dst_addr = rte_cpu_to_be_32(ctx->dst_ip_addr_start + packet_id);
      ip->total_length =
          rte_cpu_to_be_16(ctx->packet_size - sizeof(struct rte_ether_hdr));
      ip->time_to_live = 64;
      ip->hdr_checksum = 0;
      ip->hdr_checksum = rte_ipv4_cksum(ip);

      // TODO 2.5: Fill the payload
      //
      // For the payload, you are free to fill it however you link.
      // The simplest way is to just memset it to a byte value.
      //
      char *payload = (char *)(ip + 1);
      uint64_t payload_len = ctx->packet_size - sizeof(struct rte_ether_hdr) -
                             sizeof(struct rte_ipv4_hdr);
      memset(payload, packet_id, payload_len);
    }

    // TODO: 2.6: Send our crafted packets on the port
    //
    // Once we have filled all mbufs in a burst, we can use
    // `rte_eth_tx_burst` to send them through the TX port.
    //
    // This function will return the number of mbufs that were
    // successfully sent, so we need to make sure to retry until
    // all mbufs are sent.
    //
    // After that, increment tx_count with the total number of packets sent.
    //
    // NOTE: `rte_eth_tx_burst` will automatically release the mbufs after
    //       they are sent, so there is no need to do that manually.
    //
    uint16_t nb_tx = 0;
    while (nb_tx < burst_size) {
      nb_tx +=
          rte_eth_tx_burst(ctx->port_id, 0, &bufs[nb_tx], burst_size - nb_tx);
    }
    tx_count += nb_tx;
    dprintf("[TX] Sent burst of %d packets\n", nb_tx);

    // Stop the worker if we haven't been able to send any
    // packets in a loop iteration
    if (nb_tx == 0)
      return 1;

    if (ctx->burst_delay) {
      rte_delay_ms(ctx->burst_delay);
    }
  }

  dprintf("[TX] Done - sent %lu packets\n", tx_count);

  return 0;
}

static int rx_worker(void *arg) {
  struct tx_context *ctx = (struct tx_context *)arg;

  struct rte_mbuf *bufs[ctx->burst_size];

  dprintf("[RX] Starting RX worker for port %d\n", ctx->port_id);

  // TASK 3: Complete the RX loop

  bool prev_rx_empty = false;
  uint64_t rx_count = 0;
  while (rx_count < ctx->packet_count) {
    // TODO 3.1: First we need to poll the port for received packets.
    //
    // We can do this using the `rte_eth_rx_burst` function using the
    // burst_size specified in the rx_context.
    //
    // This function will also allocate the mbufs for us, unlike in the
    // TX loop where we had to do that manually.
    //
    uint16_t nb_rx = 0;
    nb_rx = rte_eth_rx_burst(ctx->port_id, 0, bufs, ctx->burst_size);

    dprintf("[RX] Received burst of %d packets\n", nb_rx);

    // Stop the worker if we have not received packets
    // for 2 consecutive loop iterations
    // if (nb_rx == 0 && prev_rx_empty)
    //   return 1;
    // prev_rx_empty = nb_rx == 0;

    for (int i = 0; i < nb_rx; i++) {
      // Parse each packet
      uint64_t packet_id = rx_count;

      // Get a pointer to the raw packet data
      char *pkt = rte_pktmbuf_mtod(bufs[i], char *);

      // TODO 3.2: Parse the Ethernet header
      //
      // We should validate that the fields of the Ethernet header
      // contain the same values that we specified in the TX loop.
      //
      // NOTE: when reading values from packet headers, make sure to
      //       use correct byte order! DPDK provides some helpers:
      //       rte_cpu_to_be_32, rte_be_to_cpu_32
      //
      struct rte_ether_hdr *eth = (struct rte_ether_hdr *)pkt;
      if (eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
        dprintf("[RX] Non-IPv4 packet, skipping\n");
        rte_pktmbuf_free(bufs[i]);
        continue;
      }

      // TODO 3.3: Parse the IPv4 header
      //
      // For the IP header, we can parse the src and dst addresses as
      // well as the payload length. We can validate the payload length
      // using the packet_size value specified in the rx_context.
      //
      // We should also validate the correctness of the hdr_checksum
      // field by calculating it the same way we did in the TX loop.
      //
      // NOTE: when reading values from packet headers, make sure to
      //       use correct byte order! DPDK provides some helpers:
      //       rte_cpu_to_be_32, rte_be_to_cpu_32
      //
      struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
      uint32_t src = 0;
      uint32_t dst = 0;
      uint16_t payload_len = 0;

      uint64_t checksum = ip->hdr_checksum;
      ip->hdr_checksum = 0;
      if (checksum != rte_ipv4_cksum(ip)) {
        dprintf("[RX] IPv4 header checksum wrong, skipping\n");
        rte_pktmbuf_free(bufs[i]);
        continue;
      }

      src = rte_be_to_cpu_32(ip->src_addr);
      dst = rte_be_to_cpu_32(ip->dst_addr);
      payload_len =
          rte_be_to_cpu_16(ip->total_length) - sizeof(struct rte_ipv4_hdr);

      dprintf("[RX] Received packet %lu: src=%u.%u.%u.%u dst=%u.%u.%u.%u "
              "payload_len=(%u)\n",
              packet_id, (src >> 24) & 0xFF, (src >> 16) & 0xFF,
              (src >> 8) & 0xFF, src & 0xFF, (dst >> 24) & 0xFF,
              (dst >> 16) & 0xFF, (dst >> 8) & 0xFF, dst & 0xFF, payload_len);

      // TODO 3.4: Parse the payload data
      //
      // Not much we can do with the payload, except maybe validate
      // that it has a value that we expect.
      //
      char *payload = (char *)(ip + 1);

      rx_count++;
    }

    // TODO 3.5: Release the mbufs
    //
    // Once we are done processing the packets, we MUST release
    // the mbufs back to the mempool. This can be done using the
    // `rte_pktmbuf_free` function for a single mbuf or the
    // `rte_pktmbuf_free_bulk` for multiple mbufs.
    //
    // Failing to release the mbufs will cause a memory leak in our
    // mempool, causing our app to eventually not be able to allocate
    // any mbufs :(
    //
    rte_pktmbuf_free_bulk(bufs, nb_rx);

    if (ctx->burst_delay) {
      rte_delay_ms(ctx->burst_delay);
    }
  }

  dprintf("[RX] Done — received %lu packets\n", rx_count);

  return 0;
}

struct stats_context {
  uint16_t rx_port_id;
  uint16_t tx_port_id;
  uint64_t packet_size;
};

static int stats_worker(void *arg) {
  struct stats_context *ctx = (struct stats_context *)arg;

  struct rte_eth_stats tx_stats, rx_stats;

  uint64_t rx_count = 0, tx_count = 0;

  dprintf("[Stats] Starting stats worker\n");

  while (true) {
    rte_eth_stats_get(ctx->tx_port_id, &tx_stats);
    rte_eth_stats_get(ctx->rx_port_id, &rx_stats);

    if (tx_count && rx_count == rx_stats.ipackets &&
        tx_count == tx_stats.opackets) {
      break;
    }

    uint64_t rx_diff = rx_stats.ipackets - rx_count;
    uint64_t tx_diff = tx_stats.opackets - tx_count;

    printf("RX: packets=%lu rate=%luGbps\n", rx_diff, rx_diff * ctx->packet_size * 8 / 1000000000);
    printf("TX: packets=%lu rate=%luGbps\n", tx_diff, tx_diff * ctx->packet_size * 8 / 1000000000);
    printf("\n");

    rx_count = rx_stats.ipackets;
    tx_count = tx_stats.opackets;

    rte_delay_ms(1000);
  }

  return 0;
}

static void port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
  int ret;

  if (!rte_eth_dev_is_valid_port(port))
    rte_panic("Invalid port %d\n", port);

  // We can get some information about the port before we
  // initialize it. This allows us to customize our options
  // based on the available hardware.
  //
  struct rte_eth_dev_info dev_info;
  ret = rte_eth_dev_info_get(port, &dev_info);
  if (ret != 0)
    rte_panic("Cannot get port %d info\n", port);

  // This is the first initialization step and allows us to
  // specify the number of RX and TX queues, as well as
  // some other advanced options which are beyond the scope
  // of this workshop :)
  //
  // We configure 1 RX and 1 TX queues, and we leave the rest
  // of the options to the default values by setting the
  // config to 0.
  //
  struct rte_eth_conf port_conf = {0};
  ret = rte_eth_dev_configure(port, 1, 1, &port_conf);
  if (ret != 0)
    rte_panic("Cannot configure port %d\n", port);

  // This step initializez a single RX queue.
  // If the port was configured with multiple RX queues, we need
  // to perform this initialization for each one.
  //
  // Notice that we have to specify an mbuf pool. The queue will
  // automatically grab mbufs from the pool to store packets
  // when we try to poll.
  //
  ret = rte_eth_rx_queue_setup(port, 0, NUM_DESC, rte_socket_id(), NULL,
                               mbuf_pool);
  if (ret < 0)
    rte_panic("Cannot setup RX queue for port %d\n", port);

  // This step initializez a single TX queue.
  // If the port was configured with multiple TX queues, we need
  // to perform this initialization for each one.
  //
  ret = rte_eth_tx_queue_setup(port, 0, NUM_DESC, rte_socket_id(),
                               &dev_info.default_txconf);
  if (ret < 0)
    rte_panic("Cannot setup TX queue for port %d\n", port);

  // Once all initialization is done, we can start the port
  ret = rte_eth_dev_start(port);
  if (ret < 0)
    rte_panic("Cannot start port %d\n", port);

  dprintf("Port %d initialized\n", port);
}

static int lcore_hello(__rte_unused void *arg) {
  printf("Hello from lcore %u\n", rte_lcore_id());
  return 0;
}

int main(int argc, char *argv[]) {
  int ret;
  uint16_t lcore_id, port_id;

  // 1. Initialization
  //
  // DPDK has many runtime configuration options:
  // allocating CPUs and memory, providing hugepages,
  // registering network interfaces, etc.
  //
  // You can list the available options by running:
  //     ./app --help
  //
  // You can see the options we use to run by checking
  // in the Makefile the RTE_RUN_FLAGS variable
  //
  ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_panic("Cannot init EAL\n");

  // 2. Lcores
  //
  // An lcore is the DPDK equivalent of a thread.
  // Using an lcore id we can spawn a task on a different CPU.
  // In general, an lcore will be pinned to a single CPU,
  // so it's best to never allocate more lcores than the
  // total number of physical CPUs.
  //
  // For our example we will use 3 lcores:
  //   - lcore 0 will be the management thread which runs
  //     the main function and orchestrates the other lcores
  //   - lcore 1 will run the RX loop
  //   - lcore 2 will run the TX loop
  //
  // We can also use the RTE_LCORE_FOREACH_WORKER to loop over
  // all available lcores (except the management one)
  //
  printf("Number of lcores: %u\n", rte_lcore_count());
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    rte_eal_remote_launch(lcore_hello, NULL, lcore_id);
  }
  rte_eal_mp_wait_lcore();

  // 3. Memory management
  //
  // An mbuf is the struct used to hold a single packet's data.
  // An mbuf pool is big chunk of memory which contains many mbufs.
  //
  // You can request an mbuf from the pool using rte_pktmbuf_alloc.
  // Once you are done with it, you can return it with rte_pktmbuf_free.
  //
  // Because the pool is preallocated and it's elements are homogenous,
  // it is much more efficient than a simple malloc/free. The downside
  // is that we need to choose the size of the pool up front. A pool that
  // is too big means that we waste memory, but a pool that is too small
  // might cause us to drop packets under bursty workloads.
  //
  struct rte_mempool *mbuf_pool;
  mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (mbuf_pool == NULL)
    rte_panic("Cannot create mbuf pool\n");

  // 4. Ports
  //
  // A port in DPDK is the equivalent of a network interface, like
  // those listed by the `ip link show` command.
  //
  // We can specify which ports to use with cli args, but we can
  // also create virtual interfaces on-the-fly. When we specify
  // a physical interface, we say that we `bind` it to DPDK,
  // taking complete ownership over it - it will no longer be
  // accesible by the kernel or other processes.
  //
  // For now, we will be creating a pair of virtual ports.
  // This is only for testing and not very interesting.
  //
  if (USE_VIRTUAL_RINGS) {
    create_virtual_ring_ports();
  }
  printf("Number of ports: %u\n", rte_eth_dev_count_avail());
  RTE_ETH_FOREACH_DEV(port_id) { port_init(port_id, mbuf_pool); }

  struct stats_context stats_ctx = {
      .rx_port_id = RX_PORT,
      .tx_port_id = TX_PORT,
      .packet_size = PACKET_SIZE,
  };
  rte_eal_remote_launch(stats_worker, &stats_ctx, 3);

  // TASK 1: Starting the RX and TX runtimes on different lcores
  unsigned rx_lcore = 1, tx_lcore = 2;

  struct rx_context rx_ctx = {
      .port_id = RX_PORT,
      .burst_size = BURST_SIZE,
      .burst_delay = BURST_DELAY,
      .packet_size = PACKET_SIZE,
      .packet_count = PACKET_COUNT,
  };
  // TODO 1.1: Launch the rx_worker function on the rx_lcore,
  //           passing the rx_ctx as argument
  rte_eal_remote_launch(rx_worker, &rx_ctx, rx_lcore);

  struct tx_context tx_ctx = {
      .port_id = TX_PORT,
      .burst_size = BURST_SIZE,
      .burst_delay = BURST_DELAY,
      .packet_size = PACKET_SIZE,
      .packet_count = PACKET_COUNT,
      .addr_count = 1,
      .src_ip_addr_start = RTE_IPV4(10, 0, 0, 1),
      .dst_ip_addr_start = RTE_IPV4(10, 1, 0, 1),
      .pool = mbuf_pool,
  };
  // TODO 1.2: Launch the tx_worker function on the tx_lcore
  //           passing the tx_ctx as argument
  rte_eal_remote_launch(tx_worker, &tx_ctx, tx_lcore);

  // TODO 1.3: Wait for the workers to finish
  rte_eal_mp_wait_lcore();

  // Query statistics to make sure all packets have been
  // sent and received.
  // printf("\n---- Port Statistics ----\n");
  // struct rte_eth_stats stats;
  // rte_eth_stats_get(TX_PORT, &stats);
  // printf("Port %u (TX): sent %lu pkts\n", TX_PORT, stats.opackets);

  // rte_eth_stats_get(RX_PORT, &stats);
  // printf("Port %u (RX): recv %lu pkts\n", RX_PORT, stats.ipackets);
  // printf("-------------------------\n");

  // BONUS TASK 1: Tracking statistics
  //
  // Instead of calculating statistics once at the end of the program,
  // we could spawn another worker which collects and displays statistics
  // once every few seconds.
  // This will allow us to track performance during the entire run.
  //
  // In order to have another lcore available, you will need to edit the
  // RTE_RUN_FLAGS in the Makefile.
  //
  // NOTE: when using `rte_eth_stats_get`, the PMD driver might not populate
  //       all available fields, but we can derive some of them ourselves.
  //       e.g. we can calculate the the total transmitted bytes by multipling
  //       the total packet count with the size of the packets.
  //

  rte_eal_cleanup();
  return 0;
}
