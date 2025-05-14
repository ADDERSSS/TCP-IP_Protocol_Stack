#include <stdio.h>
#include "sys_plat.h"
#include "echo/tcp_echo_client.h"
#include "echo/tcp_echo_server.h"
#include "net.h"
#include "netif_pcap.h"

static sys_mutex_t mutex;
static sys_sem_t sem;
static int cc;

static char buffer_g[100];
static int write_idx, read_idx, total;
static sys_sem_t read_sem, write_sem;
static sys_mutex_t sz_mutex;

void thread1_entry (void * arg) {
	for (int i = 0; i < 2 * sizeof(buffer_g); i ++) {
		sys_sem_wait(read_sem, 0);
		uint8_t data = buffer_g[read_idx++];

		if (read_idx >= sizeof(buffer_g)) {
			read_idx = 0;
		}

		sys_mutex_lock(sz_mutex);
		total --;
		sys_mutex_unlock(sz_mutex);

		plat_printf("thread1 : read data = %d\n", data);
		sys_sem_notify(write_sem);
		sys_sleep(100);
	}

	while (1) {
		plat_printf("this is thread1 : %d\n", cc);
		cc ++;
		sys_sleep(1000);
		sys_sem_notify(sem);
		sys_sleep(1000);
	}
}

void thread2_entry (void * arg) {
	sys_sleep(1000);

	for (int i = 0; i < 2 * sizeof(buffer_g); i ++) {
		sys_sem_wait(write_sem, 0);
		buffer_g[write_idx++] = i;
		if (write_idx >= sizeof(buffer_g)) {
			write_idx = 0;
		}

		sys_mutex_lock(sz_mutex);
		total ++;
		sys_mutex_unlock(sz_mutex);

		plat_printf("thread2 : write data = %d\n", i);
		sys_sem_notify(read_sem);
	}

	while (1) {
		sys_sem_wait(sem, 0);
		plat_printf("this is thread2 : %d\n", cc);
		cc ++;
	}
}

net_err_t netdev_init (void) {
	netif_pcap_open();

	return NET_ERR_OK;
}


int main (void) {
	net_init();

	net_start();

	netdev_init();

	while (1) {
		sys_sleep(10);
	}

	return 0;
} 