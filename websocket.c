#include "websocket.h"

const char sec_websocket_key[] = "Sec-WebSocket-Key:";
const unsigned int sec_websocket_key_size = 18;

const char response[] = "HTTP/1.1 101 Switching Protocols\r\n"
						"Upgrade: websocket\r\n"
						"Connection: Upgrade\r\n"
						"Sec-WebSocket-Accept: %s\r\n\r\n";
const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const char close_sequence[] = {0x88, 0x02, 0x03, 0xe8};


int websocket_get_key ( char * tcp_response_buf, u16_t max_buf_size, websocket_t * websocket )
{
	unsigned int counter = max_buf_size;

	if ( (tcp_response_buf[0] =! 'G') && ( tcp_response_buf[1] =! 'E') ) return 0;

	while ( (counter-- > 0) && (tcp_response_buf != NULL) )
	{
		if ( ( *tcp_response_buf == 'S' ) && ( *( tcp_response_buf + 1 ) == 'e' ) )
		{
			if ( counter < 43 ) return 0;
			if( strncasecmp( tcp_response_buf, sec_websocket_key, sec_websocket_key_size ) == 0 )
			{
				memcpy ( websocket->client_key, tcp_response_buf + 19, 24);
				websocket->client_key[24] = 0x00;
				return 1;
			}
		}
		tcp_response_buf++;
	}
	return 0;
}

int websocket_encode_key ( websocket_t * websocket )
{
	strlcpy(( char * )&websocket->client_key[24], guid, 64);

	unsigned char sha1sum[20];
	mbedtls_sha1((unsigned char *) websocket->client_key, sizeof(guid) + 23, sha1sum);

	unsigned int olen;
	mbedtls_base64_encode(NULL, 0, &olen, sha1sum, 20);
	mbedtls_base64_encode(websocket->encoded_key, sizeof(websocket->encoded_key), &olen, sha1sum, 20);
	websocket->encoded_key[28] = 0x00;

	return 1;
}

websocket_t * new_websocket ( ip_addr_t * ip, u16_t port ){

	websocket_t * websocket = (websocket_t *) pvPortMallocCaps(sizeof(websocket_t), MALLOC_CAP_8BIT);
	if ( websocket == NULL ) return NULL;
	websocket->ip = ip;
	websocket->port = port;
	websocket->status = websocket_status_closed;
	websocket->listen_conn = tcp_new();

	if ( websocket->listen_conn == NULL ) return 0;

	if ( tcp_bind( websocket->listen_conn, ip, port ) == ERR_OK ){
		websocket->listen_conn = tcp_listen( websocket->listen_conn );

		tcp_arg( websocket->listen_conn, websocket );
		tcp_accept( websocket->listen_conn, websocket_accept );

		return websocket;
	}
	else
	{
		vPortFree(websocket);
		return NULL;
	}
}

err_t websocket_accept(void *arg, struct tcp_pcb *newpcb, err_t err){
	websocket_t * websocket = ( websocket_t * ) arg;

	if ( websocket->status == websocket_status_closed ){

		websocket->server_conn = newpcb;

		tcp_arg( websocket->server_conn, websocket );
		tcp_recv( websocket->server_conn, websocket_received );
		tcp_sent( websocket->server_conn, websocket_sent );

		tcp_accepted( websocket->listen_conn );
	}
	else {
		tcp_accepted( websocket->listen_conn );
		tcp_close ( newpcb );
	}

	return ERR_OK;
}

err_t websocket_received(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err){
	websocket_t * websocket = ( websocket_t * ) arg;
	
	websocket_frame_t * frame = NULL;

	tcp_arg( websocket->server_conn, websocket );
	tcp_recv( websocket->server_conn, websocket_received );
	tcp_sent( websocket->server_conn, websocket_sent );

	if ( p == NULL ){
		tcp_close ( tpcb );
		//if ( websocket->onclose ) websocket->onclose();
		websocket->status = websocket_status_closed;
		return ERR_OK;
	}

	switch ( websocket->status ){
	case websocket_status_closed:

		if ( websocket_get_key( p->payload, p->len, websocket ) ){
			char * buffer = (char *) pvPortMallocCaps(256, MALLOC_CAP_8BIT);
			websocket_encode_key( websocket );
			snprintf( buffer, 256, response, websocket->encoded_key);
			tcp_write( tpcb, buffer, strlen(buffer), TCP_WRITE_FLAG_COPY );
			vPortFree(buffer);
			websocket->status = websocket_status_open;
			if ( websocket->onopen ) websocket->onopen();
		}

		break;
	case websocket_status_open:

		frame = websocket_decode_frame( p->payload, p->len );

		if ( frame->opcode == websocket_opcode_close ){
			websocket->status = websocket_status_closed;
			tcp_write( tpcb, p->payload, p->len, TCP_WRITE_FLAG_COPY );
			if ( websocket->onclose ) websocket->onclose();
		}
		if ( frame->opcode == websocket_opcode_text || frame->opcode == websocket_opcode_binary ){
			if ( websocket->onmessage ) websocket->onmessage( frame );
		}

		if (frame->data) vPortFree(frame->data);
		if (frame) vPortFree(frame);

		break;
	}

	if ( p != NULL ){
		tcp_recved( tpcb, p->tot_len );
		pbuf_free( p );
	}
	return ERR_OK;
}

err_t websocket_sent(void *arg, struct tcp_pcb *tpcb, u16_t len){
	websocket_t * websocket = ( websocket_t * ) arg;

	LWIP_UNUSED_ARG( websocket );

	return ERR_OK;
}

websocket_frame_t * websocket_decode_frame ( char * frame_ptr, u16_t len ){

	u8_t masking_key[4];
	u16_t data_len = frame_ptr[1] & 0x7f;
	websocket_frame_t * frame = (websocket_frame_t *) pvPortMallocCaps(sizeof(websocket_frame_t), MALLOC_CAP_8BIT);

	if ( frame == NULL ) return 0;

	if ( data_len < 126 ) {

		frame->opcode = frame_ptr[0];
		frame->length = frame_ptr[1] & 0x7f;

		frame->data = (u8_t *) pvPortMallocCaps(frame->length + 1, MALLOC_CAP_8BIT);

		if ( frame->data == NULL ){
			vPortFree(frame);
			return NULL;
		}

		for ( int i = 0; i < 4; i++ ){
			masking_key[i] = frame_ptr[2 + i];
		}
		memcpy ( frame->data, &frame_ptr[6], frame->length );

		for ( int i = 0; i < frame->length; i++ ){
			frame->data[i] ^= masking_key[ i % 4 ];
		}

		frame->data[ frame->length ] = 0x00;

		return frame;
	}

	else {
		frame->opcode = frame_ptr[0];
		frame->length = ( u16_t )( ( frame_ptr[2] << 8 ) | frame_ptr[3] );
		frame->data = (u8_t *) pvPortMallocCaps(frame->length + 1, MALLOC_CAP_8BIT);

		if ( frame->data == NULL ){
			vPortFree(frame);
			return NULL;
		}

		for ( int i = 0; i < 4; i++ ){
			masking_key[i] = frame_ptr[4 + i];
		}
		memcpy ( frame->data, &frame_ptr[8], frame->length );

		for ( int i = 0; i < frame->length; i++ ){
			frame->data[i] ^= masking_key[ i % 4 ];
		}

		frame->data[ frame->length ] = 0x00;

		return frame;
	}
	return NULL;
}

u8_t * websocket_encode_frame( websocket_opcode_t opcode, u8_t * data, u16_t len ){

	if ( data == NULL || len == 0  || len > 0xffff - 4 ) return NULL;

	if ( len < 126 ){
		u8_t * data_ptr = (u8_t *) pvPortMallocCaps(len + 3, MALLOC_CAP_8BIT);

		if ( data_ptr == NULL ) return NULL;

		data_ptr[0] = ( u8_t )opcode;
		data_ptr[1] = len;
		memcpy ( &data_ptr[2], data, len );

		data_ptr[ len + 2 ] = 0;

		return data_ptr;
	}
	else {
		u8_t * data_ptr = (u8_t *) pvPortMallocCaps(len + 5, MALLOC_CAP_8BIT);

		if ( data_ptr == NULL ) return NULL;

		data_ptr[0] = ( u8_t )opcode;
		data_ptr[1] = 126;
		data_ptr[2] = ( u8_t )( len >> 8 );
		data_ptr[3] = ( u8_t )( len );

		memcpy ( &data_ptr[4], data, len );

		data_ptr[ len + 4 ] = 0;

		return data_ptr;
	}
	return NULL;
}

err_t websocket_send_api_call(struct tcpip_api_call* call){

	api_call_t * send_api_data = ( api_call_t * ) call;
	err_t return_val = ERR_MEM;

	if ( send_api_data->websocket->server_conn && send_api_data->data ){

		return_val = tcp_write( send_api_data->websocket->server_conn, send_api_data->data, strlen ( ( char * )send_api_data->data ), TCP_WRITE_FLAG_COPY );
		tcp_output( send_api_data->websocket->server_conn );

	}

	if (send_api_data->data) vPortFree(send_api_data->data);

	return return_val;
}

int websocket_send ( websocket_t * websocket, u8_t * data, u16_t len ){

	if ( websocket->status != websocket_status_open ) return 0;
	u8_t * data_ptr = websocket_encode_frame( websocket_opcode_text, data, len );

	if ( data_ptr == NULL ) return 0;

	err_t return_val;

	api_call_t send_api_data;
	send_api_data.data = data_ptr;
	send_api_data.websocket = websocket;

	return_val = tcpip_api_call( websocket_send_api_call, (struct tcpip_api_call*)&send_api_data );

	if( return_val == ERR_OK ) return 1;
	else return 0;
}

