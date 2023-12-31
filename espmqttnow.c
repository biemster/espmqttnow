/*
Florenc Caminade
Thomas FLayols
Etienne Arlaud

Receive raw 802.11 packet and filter ESP-NOW vendor specific action frame using BPF filters.
https://hackaday.io/project/161896
https://github.com/thomasfla/Linux-ESPNOW

Adapted from :
https://stackoverflow.com/questions/10824827/raw-sockets-communication-over-wifi-receiver-not-able-to-receive-packets

1/Find your wifi interface:
$ iwconfig

2/Setup your interface in monitor mode :
$ sudo ifconfig wlp5s0 down
$ sudo iwconfig wlp5s0 mode monitor
$ sudo ifconfig wlp5s0 up

3/Run this code as root
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "mqtt.h"
#include "posix_sockets.h"

#define MAX_PACKET_LEN 1000
#define LEN_RAWBYTES_MAX 512
#define WLAN_LEN 24
#define ACTIONFRAME_HEADER_LEN 8
#define VENDORSPECIFIC_CONTENT_LEN 7

//filter action frame packets
//Equivalent for tcp dump :
//type 0 subtype 0xd0 and wlan[24:4]=0x7f18fe34 and wlan[32]=221 and wlan[33:4]&0xffffff = 0x18fe34 and wlan[37]=0x4
//NB : There is no filter on source or destination addresses, so this code will 'receive' the action frames sent by this computer...
#define FILTER_LENGTH 20
static struct sock_filter bpfcode[FILTER_LENGTH] = {
	{ 0x30, 0,  0, 0x00000003 },	// ldb [3]	// radiotap header length : MS byte
	{ 0x64, 0,  0, 0x00000008 },	// lsh #8	// left shift it
	{ 0x07, 0,  0, 0x00000000 },	// tax		// 'store' it in X register
	{ 0x30, 0,  0, 0x00000002 },	// ldb [2]	// radiotap header length : LS byte
	{ 0x4c, 0,  0, 0x00000000 },	// or  x	// combine A & X to get radiotap header length in A
	{ 0x07, 0,  0, 0x00000000 },	// tax		// 'store' it in X
	{ 0x50, 0,  0, 0x00000000 },	// ldb [x + 0]		// right after radiotap header is the type and subtype
	{ 0x54, 0,  0, 0x000000fc },	// and #0xfc		// mask the interesting bits, a.k.a 0b1111 1100
	{ 0x15, 0, 10, 0x000000d0 },	// jeq #0xd0 jt 9 jf 19	// compare the types (0) and subtypes (0xd)
	{ 0x40, 0,  0, 0x00000018 },	// Ld  [x + 24]			// 24 bytes after radiotap header is the end of MAC header, so it is category and OUI (for action frame layer)
	{ 0x15, 0,  8, 0x7f18fe34 },	// jeq #0x7f18fe34 jt 11 jf 19	// Compare with category = 127 (Vendor specific) and OUI 18:fe:34
	{ 0x50, 0,  0, 0x00000020 },	// ldb [x + 32]				// Begining of Vendor specific content + 4 ?random? bytes : element id
	{ 0x15, 0,  6, 0x000000dd },	// jeq #0xdd jt 13 jf 19		// element id should be 221 (according to the doc)
	{ 0x40, 0,  0, 0x00000021 },	// Ld  [x + 33]				// OUI (again!) on 3 LS bytes
	{ 0x54, 0,  0, 0x00ffffff },	// and #0xffffff			// Mask the 3 LS bytes
	{ 0x15, 0,  3, 0x0018fe34 },	// jeq #0x18fe34 jt 16 jf 19		// Compare with OUI 18:fe:34
	{ 0x50, 0,  0, 0x00000025 },	// ldb [x + 37]				// Type
	{ 0x15, 0,  1, 0x00000004 },	// jeq #0x4 jt 18 jf 19			// Compare type with type 0x4 (corresponding to ESP_NOW)
	{ 0x06, 0,  0, 0x00040000 },	// ret #262144	// return 'True'
	{ 0x06, 0,  0, 0x00000000 },	// ret #0	// return 'False'
};

static uint8_t espnow_ethheader[77] = {
	0x00, 0x00, 0x26, 0x00, 0x2f, 0x40, 0x00, 0xa0, 0x20, 0x08, 0x00, 0xa0, 0x20, 0x08, 0x00, 0x00,
	0xdf, 0x32, 0xfe, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0c, 0x6c, 0x09, 0xc0, 0x00, 0xd3, 0x00,
	0x00, 0x00, 0xd3, 0x00, 0xc7, 0x01, 0xd0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x70, 0x51, 0x7f, 0x18,
	0xfe, 0x34, 0xa2, 0x03, 0x92, 0xb0, 0xdd, 0xff, 0x18, 0xfe, 0x34, 0x04, 0x01
};
#define ETHHEADER_SIZE (sizeof(espnow_ethheader) * sizeof(uint8_t))

int raw_wlan_sock_fd; // globals should be avoided

void* mqtt_client_refresher(void* client) {
	while(1) {
		mqtt_sync((struct mqtt_client*) client);
		usleep(1000U); // 1 ms
	}
	return NULL;
}

int ESPNOW_get_radiotap_len(uint8_t *raw_bytes, int len) {
	if(len < 4) return -1;
	return (int)raw_bytes[2] + ((int)raw_bytes[3] << 8);
}

uint8_t* ESPNOW_get_src_mac(uint8_t *raw_bytes, int len) {
	int radiotap_len = ESPNOW_get_radiotap_len(raw_bytes, len);

	if(len < radiotap_len + 10 + 6) return NULL;

	return raw_bytes + radiotap_len + 10;
}

int ESPNOW_get_payload_len(uint8_t *raw_bytes, int len) {
	int radiotap_len = ESPNOW_get_radiotap_len(raw_bytes, len);
	
	if(len < radiotap_len + WLAN_LEN + ACTIONFRAME_HEADER_LEN + 1) return -1;

	return raw_bytes[radiotap_len + WLAN_LEN + ACTIONFRAME_HEADER_LEN + 1] - 5;
}

uint8_t* ESPNOW_get_payload(uint8_t *raw_bytes, int len) {
	int radiotap_len = ESPNOW_get_radiotap_len(raw_bytes, len);

	if(len < radiotap_len + WLAN_LEN + ACTIONFRAME_HEADER_LEN + VENDORSPECIFIC_CONTENT_LEN) return NULL;

	return raw_bytes + radiotap_len + WLAN_LEN + ACTIONFRAME_HEADER_LEN + VENDORSPECIFIC_CONTENT_LEN;
}

size_t ESPNOW_create_packet(uint8_t *raw_bytes, uint8_t *payload, int len) {
	memcpy(raw_bytes, espnow_ethheader, ETHHEADER_SIZE);

	uint8_t src_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	uint8_t dst_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	memcpy(raw_bytes +42, dst_mac, sizeof(uint8_t) *6);
	memcpy(raw_bytes +48, src_mac, sizeof(uint8_t) *6);
	memcpy(raw_bytes +54, dst_mac, sizeof(uint8_t) *6);
	
	raw_bytes[71] = len +5;
	memcpy(raw_bytes +ETHHEADER_SIZE, payload, len);

	return ETHHEADER_SIZE + len + 4;
}

void publish_callback(void** unused, struct mqtt_response_publish *published) {
	/* note that published->topic_name AND msg are NOT null-terminated (here we'll change it to a c-string) */
	char* topic_name = (char*) malloc(published->topic_name_size + 1);
	memcpy(topic_name, published->topic_name, published->topic_name_size);
	topic_name[published->topic_name_size] = '\0';

	uint8_t* msg = (uint8_t*) malloc(published->application_message_size + 1);
	memcpy(msg, published->application_message, published->application_message_size);
	msg[published->application_message_size] = '\0';

	printf("Received publish('%s'): %s, broadcasting it over ESPNOW\n", topic_name, msg);
	free(topic_name);

	uint8_t raw_bytes[LEN_RAWBYTES_MAX] = {0};
	int raw_len = ESPNOW_create_packet(raw_bytes, msg, published->application_message_size);
	sendto(raw_wlan_sock_fd, raw_bytes, raw_len, 0, NULL, 0);
}

int main(int argc, char **argv) {
	assert(argc == 4);

	uint8_t buff[MAX_PACKET_LEN] = {0};
	char *dev = argv[1];
	struct sock_fprog bpf = {FILTER_LENGTH, bpfcode};

	raw_wlan_sock_fd = create_raw_socket(dev, &bpf); /* Creating the raw socket */

	printf("* Connecting MQTT ");
	int sockfd = open_nb_socket(argv[2], argv[3]);
	struct mqtt_client client; /* instantiate the client */
	uint8_t sendbuf[2048]; /* sendbuf should be large enough to hold multiple whole mqtt messages */
	uint8_t recvbuf[1024]; /* recvbuf should be large enough any whole mqtt message expected to be received */
	mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);
	/* Create an anonymous session */
	const char* client_id = NULL;
	/* Ensure we have a clean session */
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
	/* Send connection request to the broker. */
	mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);
	if (client.error == MQTT_OK) {
		printf("OK\n");
	}

	/* start a thread to refresh the client (handle egress and ingress client traffic) */
	pthread_t client_daemon;
	if(pthread_create(&client_daemon, NULL, mqtt_client_refresher, &client)) {
		fprintf(stderr, "Failed to start client daemon.\n");
	}

	/* subscribe */
	mqtt_subscribe(&client, "espnow/broadcast", 0);


	printf("* Waiting to receive packets\n");

	while(1) {
		int len = recvfrom(raw_wlan_sock_fd, buff, MAX_PACKET_LEN, MSG_TRUNC, NULL, 0);

		if(len < 0) {
			perror("Socket receive failed or error");
			break;
		}
		else {
			uint8_t *src = ESPNOW_get_src_mac(buff, len);
			char src_mac_str[18] = {0};
			sprintf(src_mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", src[0], src[1], src[2], src[3], src[4], src[5]);
			int msglen = ESPNOW_get_payload_len(buff, len);
			uint8_t *msg = ESPNOW_get_payload(buff, len);

			printf("new packet from %s, len:%d\n", src_mac_str, msglen);
			char topic[12+18] = {0};
			sprintf(topic, "espnow/%s", src_mac_str);
			mqtt_publish(&client, topic, msg, msglen, MQTT_PUBLISH_QOS_0);

			/* check for errors */
			if (client.error != MQTT_OK) {
				fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
			}
		}
	}

	close(raw_wlan_sock_fd);
	return 0;
}
