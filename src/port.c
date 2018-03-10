#include "pktsender.h"
#include "util.h"
#include "port.h"

#include <rte_memory.h>
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>

static struct rte_eth_conf port_eth_conf = {
	.rxmode = {
		.split_hdr_size = 0,
		.header_split   = 0,	/**< Header Split disabled. */
		.hw_ip_checksum = 0,	/**< IP checksum offload disabled. */
		.hw_vlan_filter = 0,	/**< VLAN filtering enabled. */
		.hw_vlan_strip  = 0,	/**< VLAN strip enabled. */
		.hw_vlan_extend = 0,	/**< Extended VLAN disabled. */
		.jumbo_frame    = 0,	/**< Jumbo Frame Support disabled. */
		.hw_strip_crc   = 0,	/**< CRC stripping by hardware disabled. */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_key_len = 0,
			.rss_hf = ETH_RSS_IP,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

static uint8_t port_nb_max = 0;
static struct port_info *port_list = NULL;

/** Check whether the port is enabled. */
bool port_is_enabled(uint8_t portid)
{
	if (portid >= port_nb_max)
		return false;

	if (port_list[portid].is_enabled)
		return true;
	return false;
}

/** Update the lcore mapping of a port job */
void port_update_lcore(uint8_t portid, uint8_t lcoreid, uint8_t job)
{
	port_list[portid].lcore_map[job] = lcoreid;
}

/**
 * Parse a portmask and initialize port_list array
 *
 * @param portmask
 * @return
 *	The number of ports enabled
 */
uint8_t port_parse_opt(unsigned long portmask)
{
//	struct port_info *info = NULL;
	uint8_t i = 0, j = 0;
	uint8_t enabled_ports = 0;

	port_nb_max = rte_eth_dev_count();
//	port_nb_max = 10;

	port_list = (struct port_info *)malloc(sizeof(struct port_info)
											* port_nb_max);
	if (port_list == NULL) {
		LOG_ERROR("Failed to allocate memory for port_info[%u].",
						port_nb_max);
		return 0;
	}

	memset(port_list, 0, sizeof(struct port_info) * port_nb_max);
	for (i = 0; i < port_nb_max; i++) {
		// check whether in use
		if ((portmask & (1ul << i)) == 0) {
			continue;
		}

		port_list[i].id = i;
		port_list[i].is_enabled = 1;

		for (j = 0; j < LCORE_JOB_MAX; j++)
			port_list[i].lcore_map[j] = RTE_MAX_LCORE;

		enabled_ports++;
	}

	if (enabled_ports > 0) {
		pktsender.port_list = port_list;
	} else {
		// No port enabled
		zfree(port_list);
	}

	return enabled_ports;
}

/* Print all enabled ports */
void port_dump()
{
	uint8_t i = 0;
	struct port_info *iter = NULL;

	for (i = 0; i < port_nb_max; i++) {
		iter = &port_list[i];
		if (!port_is_enabled(i))
			continue;

		LOG_INFO("Port %u: MAC %02x:%02x:%02x:%02x:%02x:%02x, "
						"RX lcore %u, TX lcore %u",
					iter->id,
					(uint32_t)(iter->mac.addr_bytes[0]),
					(uint32_t)(iter->mac.addr_bytes[1]),
					(uint32_t)(iter->mac.addr_bytes[2]),
					(uint32_t)(iter->mac.addr_bytes[3]),
					(uint32_t)(iter->mac.addr_bytes[4]),
					(uint32_t)(iter->mac.addr_bytes[5]),
					iter->lcore_map[LCORE_JOB_RX],
					iter->lcore_map[LCORE_JOB_TX]);
	}
}

/* Initialize RX and TX queues of the port <portid> */
static int __port_init_dev(struct port_info *port,
				struct rte_eth_dev_info *dev_info)
{
	int ret = 0;

	rte_eth_dev_info_get(port->id, dev_info);

	/* get mac address of this port */
	rte_eth_macaddr_get(port->id, &port->mac);

	/* check TX queue numbers */
	if (dev_info->max_tx_queues < TXQ_NUM_PER_PORT) {
		LOG_ERROR("port %u doesn't have enough TX queue "
						"(at least %u TX queues)",
						port->id, TXQ_NUM_PER_PORT);
		return ERR_OUT_OF_RANGE;
	}

	/* configure RX and TX queues of this port */
	ret = rte_eth_dev_configure(port->id, RXQ_NUM_PER_PORT,
					TXQ_NUM_PER_PORT, &port_eth_conf);
	if (ret < 0) {
		LOG_ERROR("Failed to configure port %u, err=%d",
						port->id, ret);
		return ERR_DPDK;
	}

	return 0;
}

/** Initialize rx mempool of a port */
static int __port_init_rx_pool(struct port_info *port,
				uint8_t socketid)
{
	char s[64];

	snprintf(s, sizeof(s), "rx_mbuf_pool_%u_%u", port->id, socketid);
	port->rx_mp = rte_pktmbuf_pool_create(s, NB_MBUFS,
					MEMPOOL_CACHE_SIZE, 0,
					RTE_MBUF_DEFAULT_BUF_SIZE, socketid);
	if (port->rx_mp == NULL) {
		LOG_ERROR("Cannot create RX mbuf pool of port %u on socket %u",
						port->id, socketid);
		return ERR_MEMORY;
	}

	LOG_DEBUG("Allocate rx mbuf pool on socket %u for port %u",
					socketid, port->id);
	return 0;
}

/** Initialize tx mempool of a port */
static int __port_init_tx_pool(struct port_info *port,
				uint8_t socketid)
{
	char s[64];

	snprintf(s, sizeof(s), "tx_mbuf_pool_%u_%u", port->id, socketid);
	port->tx_mp = rte_pktmbuf_pool_create(s, NB_MBUFS,
					MEMPOOL_CACHE_SIZE, 0,
					RTE_MBUF_DEFAULT_BUF_SIZE, socketid);
	if (port->tx_mp == NULL) {
		LOG_ERROR("Cannot create TX mbuf pool of port %u on socket %u",
						port->id, socketid);
		return ERR_MEMORY;
	}

	LOG_DEBUG("Allocate tx mbuf pool on socket %u for port %u",
					socketid, port->id);
	return 0;
}

/** setup tx queue */
static int __setup_tx_queue(uint8_t portid, uint8_t queueid,
				uint8_t lcoreid, struct rte_eth_txconf *txconf)
{
	int ret;
	uint8_t socketid = 0;

	txconf->txq_flags = 0;
	socketid = (uint8_t)rte_lcore_to_socket_id(lcoreid);

	LOG_DEBUG("Setup port %u, txq %u, lcore %u, socket %u",
					portid, TXQ_TX, lcoreid, socketid);

	ret = rte_eth_tx_queue_setup(portid, queueid, TX_DESC_DEFAULT,
					socketid, txconf);
	if (ret < 0) {
		LOG_ERROR("Failed to setup txq: err=%d, port %u, txq %u",
						ret, portid, TXQ_TX);
		return ERR_DPDK;
	}
	return 0;
}

/** Initialize a single port */
static int __port_init_single(uint8_t portid)
{
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf *txconf = NULL;
	struct port_info *port = NULL;
	uint8_t lcoreid, socketid, job = 0;
	int ret;

	port = &port_list[portid];

	ret = __port_init_dev(port, &dev_info);
	if (ret < 0) {
		LOG_ERROR("Failed to configure RX/TX queues of port %u", portid);
		return ret;
	}

	/*  setup RX and TX queues */
	for (job = 0; job < LCORE_JOB_MAX; job++) {
		lcoreid = port->lcore_map[job];
		if (lcoreid == RTE_MAX_LCORE) {
			LOG_WARN("No lcore is assigned to port %u job %u.", portid, job);
			continue;
		}

		socketid = (uint8_t)rte_lcore_to_socket_id(lcoreid);

		/* setup RX queue */
		if (job == LCORE_JOB_RX) {
			ret = __port_init_rx_pool(port, socketid);
			if (ret < 0)
				return ERR_MEMORY;

			LOG_DEBUG("Setup port %u, rxq %u, lcore %u, socket %u",
							portid, RXQ_RX, lcoreid, socketid);

			ret = rte_eth_rx_queue_setup(portid, RXQ_RX,
							RX_DESC_DEFAULT, socketid, NULL,
							port->rx_mp);
			if (ret < 0) {
				LOG_ERROR("Failed to setup rxq: err=%d, port %u, rxq %u",
								ret, portid, RXQ_RX);
				goto fail_free_mp;
			}
		} else {
			ret = __port_init_tx_pool(port, socketid);
			if (ret < 0)
				goto fail_free_mp;

			txconf = &dev_info.default_txconf;

			/* setup TX queue */
			ret = __setup_tx_queue(portid, TXQ_TX, lcoreid, txconf);
			if (ret < 0)
				goto fail_free_mp;

			/* init tx controller */
			tx_ctl_init(&port->tx_ctl, &port->mac, TXQ_TX);
			/* setup default packets */
			tx_ctl_setup_mempool(&port->tx_ctl, port->tx_mp);

			/* setup TX probe queue */
			ret = __setup_tx_queue(portid, TXQ_LATENCY,
							pktsender.stat_lcore, txconf);
			if (ret < 0)
				goto fail_free_mp;
		}
	}
	return 0;

fail_free_mp:
	/* free rx mempool */
	if (port->rx_mp) {
		rte_mempool_free(port->rx_mp);
		port->rx_mp = NULL;
	}

	/* free tx mempool */
	if (port->tx_mp) {
		rte_mempool_free(port->tx_mp);
		port->tx_mp = NULL;
	}
	return ret;
}

/* Free a port_info structure */
void __port_free_single(uint8_t portid)
{
	struct port_info *port = NULL;

	if (!port_is_enabled(portid))
		return;

	port = &port_list[portid];

	if (port->rx_mp) {
		rte_mempool_free(port->rx_mp);
		port->rx_mp = NULL;
	}

	if (port->tx_mp) {
		rte_mempool_free(port->tx_mp);
		port->tx_mp = NULL;
	}
}

/* Initialize all ports */
int port_init(void)
{
	uint8_t portid = 0, i = 0;
	int ret = 0;

	for(portid = 0; portid < port_nb_max; portid++) {
		if (!port_is_enabled(portid))
			continue;

		ret = __port_init_single(portid);
		if (ret < 0) {
			LOG_ERROR("Failed to initialize port %u", portid);
			goto fail_clear;
		}
	}

	LOG_DEBUG("Init port_list");
	return 0;

fail_clear:
	for (i = 0; i < portid; i++)
		__port_free_single(i);
	return ret;
}

/* Start all ports */
int port_start(void)
{
	int ret = 0;
	uint8_t portid= 0, i = 0;

	for (portid = 0; portid < port_nb_max; portid++) {
		if (!port_is_enabled(portid))
			continue;

		/* start device */
		LOG_DEBUG("Start port %u", portid);
		ret = rte_eth_dev_start(portid);
		if (ret < 0) {
			LOG_ERROR("Failed to start eth device %u, err=%d",
							portid, ret);
			goto fail_close_port;
		}

		/* set promiscuous mode */
		rte_eth_promiscuous_enable(portid);

		/* Enable timesync timestamping for the Ethernet device */
		rte_eth_timesync_enable(portid);
		LOG_DEBUG("Enable timesync for port %u", portid);
	}

	LOG_DEBUG("Start all ports");
	return 0;

fail_close_port:
	for (i = 0; i < portid; i++) {
		if (port_is_enabled(i)) {
			rte_eth_dev_stop(i);
			rte_eth_dev_close(i);
		}
	}
	return ERR_DPDK;
}

/* Free port_list and all related memory */
void port_free(void)
{
	uint8_t portid = 0;

	if (port_list == NULL)
		return;

	for (portid = 0; portid < port_nb_max; portid++)
		__port_free_single(portid);

	zfree(port_list);
	pktsender.port_list = NULL;

	LOG_DEBUG("Free port_list");
}

/* Close all ports */
void port_close(void)
{
	uint8_t portid = 0;

	for (portid = 0; portid < port_nb_max; portid++) {
		if (!port_is_enabled(portid))
			continue;

		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
	}

	LOG_DEBUG("Close all ports");
}

/* send on all ports */
int port_transmit(uint8_t portid)
{
	return tx_ctl_tx_burst(portid, &port_list[portid].tx_ctl,
						port_list[portid].tx_mp);
}

/* get pkt_seq */
struct pkt_seq *
port_get_pkt_seq(uint8_t portid)
{
	return &(port_list[portid].tx_ctl.tx_seq);
}
