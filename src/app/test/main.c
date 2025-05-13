#include <stdio.h>
#include "sys_plat.h"
#include "echo/tcp_echo_client.h"

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

int main (void) {

	sem = sys_sem_create(0);  
	read_sem = sys_sem_create(0);
	write_sem = sys_sem_create(sizeof(buffer_g));
	mutex = sys_mutex_create();
	sz_mutex = sys_mutex_create();

	// sys_thread_create(thread1_entry, "AAAA");
	// sys_thread_create(thread2_entry, "BBBB");

	tcp_echo_client_start(friend0_ip, 5000);

	//static const uint8_t netdev0_hwaddr[] = { 0x00, 0x50, 0x56, 0xc0, 0x00, 0x11 };
	pcap_t* pcap = pcap_device_open(netdev0_phy_ip, netdev0_hwaddr);
	while (pcap) {
		static uint8_t buffer[1024];
		static int counter; 
		struct pcap_pkthdr * pkthdr;
		const uint8_t * pkt_data;

		plat_printf("begin test : %d\n", counter++);
		for (int i = 0; i < sizeof(buffer); i ++) {
			buffer[i] = i;
		}

		if (pcap_next_ex(pcap, &pkthdr, &pkt_data) != 1) {
			continue;
		}

		int len = pkthdr->len > sizeof(buffer) ? sizeof(buffer) : pkthdr->len;
		plat_memcpy(buffer, pkt_data, len);

		buffer[0] = 1;
		buffer[1] = 2;

		if (pcap_inject(pcap, buffer, sizeof(buffer)) == -1) {
			plat_printf("pcap send: send packet failed %s \n", pcap_geterr(pcap));
			break;
		}
	}

	printf("Hello, world");
	return 0;
} 