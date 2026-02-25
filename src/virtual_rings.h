#include <rte_eal.h>
#include <rte_eth_ring.h>

/*
 * Create a pair of connected ports for testing
 */
static void create_virtual_ring_ports(void) {
  struct rte_ring *ring_a = rte_ring_create("RING_A", 1024, rte_socket_id(),
                                            RING_F_SP_ENQ | RING_F_SC_DEQ);
  struct rte_ring *ring_b = rte_ring_create("RING_B", 1024, rte_socket_id(),
                                            RING_F_SP_ENQ | RING_F_SC_DEQ);
  if (ring_a == NULL || ring_b == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create rings\n");

  if (rte_eth_from_rings("port0", &ring_b, 1, &ring_a, 1, rte_socket_id()) < 0)
    rte_exit(EXIT_FAILURE, "Cannot create port 0\n");

  if (rte_eth_from_rings("port1", &ring_a, 1, &ring_b, 1, rte_socket_id()) < 0)
    rte_exit(EXIT_FAILURE, "Cannot create port 1\n");
}
