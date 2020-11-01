#include <stdlib.h>
#include <string.h>

#include "lwip/api.h"
#include "lwip/priv/tcpip_priv.h"
#include "lwip/tcp.h"
#include "lwip/opt.h"

#include "freertos/queue.h"
#include "freertos/FreeRTOS.h"

#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

#include "esp_intr_alloc.h"
#include "esp_heap_alloc_caps.h"
#include "esp_log.h"

extern const char sec_websocket_key[];
extern const unsigned int sec_websocket_key_size;

extern const char response[];
extern const char guid[];
extern const char close_sequence[];


#define WEBSOCKET_SEND_QUEUESIZE 10
#define WEBSOCKET_RECEIVE_QUEUESIZE 10

#define WEBSOCKET_SEND_WAIT_TIME 1000
#define WEBSOCKET_RECEIVE_WAIT_TIME 1000
 
typedef enum {
	websocket_opcode_text = 0x81,
	websocket_opcode_binary = 0x85,
	websocket_opcode_close = 0x88,
	websocket_opcode_ping = 0x9,
	websocket_opcode_pong = 0x8A,
} websocket_opcode_t;

typedef struct{
	websocket_opcode_t opcode;
	u8_t * data;
	u16_t length;
} websocket_frame_t;

typedef enum {
	websocket_status_closed,
	websocket_status_open,
} websocket_status_t;

typedef void ( * websocket_onopen_fn )( void );
typedef void ( * websocket_onmessage_fn )( websocket_frame_t * );
typedef void ( * websocket_onclose_fn )( void );
typedef void ( * websocket_onerror_fn )( void );

// #pragma pack( push, 4 )
typedef struct {
    struct tcp_pcb * listen_conn;
	struct tcp_pcb * server_conn;
	websocket_onopen_fn onopen;
	websocket_onmessage_fn onmessage;
	websocket_onclose_fn onclose;
	websocket_onerror_fn onerror;
//    QueueHandle_t send_queue;
//    QueueHandle_t receive_queue;
//    TaskHandle_t send_task;
//    TaskHandle_t receive_task;
	ip_addr_t * ip;
	u8_t client_key[25];
	u8_t encoded_key[29];
	websocket_status_t status;
	u16_t port;
} websocket_t;
// #pragma pack ( pop )

typedef struct {
	struct tcpip_api_call call;
	websocket_t * websocket;
	u8_t * data;
} api_call_t;

websocket_t * new_websocket ( ip_addr_t * ip, u16_t port );

err_t websocket_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
err_t websocket_received(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
err_t websocket_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);

websocket_frame_t * websocket_decode_frame ( char * frame_ptr, u16_t len );
u8_t * websocket_encode_frame( websocket_opcode_t opcode, u8_t * data, u16_t len );

err_t websocket_send_api_call(struct tcpip_api_call* call);
int websocket_send( websocket_t * websocket, u8_t * data, u16_t len );
