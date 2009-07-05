#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#define PORT_DEFAULT 2706
#define BUFSIZE 4096

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

typedef enum _nc_mode { MODE_NONE, MODE_GET, MODE_SEND } nc_mode;

nc_mode mode = MODE_NONE;
int port = PORT_DEFAULT;
char *host = NULL;

void usage( char *cmd )
{
	printf( "Usage: %s (get|send) (<host>) (<port>)\n", cmd );
	printf( "Examples:\n" );
	printf( "%s send 192.168.0.2\n", cmd );
	printf( "%s send 192.168.0.2 2706\n", cmd );
	printf( "%s get\n", cmd );
	printf( "%s get 2706\n", cmd );
}

int parse_cmdline( int argc, char *argv[] )
{
	char *port_str = NULL;

	if ( argc < 2 )
		return FALSE;
	
	if ( strcmp( argv[1], "get" ) == 0 )
		mode = MODE_GET;

	if ( strcmp( argv[1], "send" ) == 0 )
		mode = MODE_SEND;

	if ( mode == MODE_NONE )
		return FALSE;
	
	if ( mode == MODE_GET ) {
		if ( ( argc < 2 ) || ( argc > 3 ) )
			return FALSE;

		if ( argc == 3 )
			port_str = argv[2];
	}

	if ( mode == MODE_SEND ) {
		if ( ( argc < 3 ) || ( argc > 4 ) )
			return FALSE;

		host = argv[2];

		if ( argc == 4 )
			port_str = argv[3];
	}

	if ( port_str != NULL ) {
		port = atoi( port_str );
		if ( port <= 0 ) {
			printf( "Invalid port number!\n" );
			return FALSE;
		}
	}

	return TRUE;
}

int nc_get( void )
{
	int sock, recv_sock;
	int result;
	struct sockaddr_in addr;
	char buf[BUFSIZE];
	ssize_t len;
#ifndef _MINIX
	int yes;
#endif

	sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

#ifndef _MINIX
	yes = 1;
	result = setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof( int ) );
	if ( result != 0 ) {
		printf( "Could not set SO_REUSEADDR on socket.\n" );
		return FALSE;
	}
#endif

	if ( sock == -1 ) {
		printf( "Could not create socket\n" );
		return FALSE;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_ANY );
	addr.sin_port = htons( port );

	if ( bind( sock, (struct sockaddr *)&addr, sizeof( struct sockaddr_in ) ) == -1 ) {
		printf( "Unable to bind: %s\n", strerror( errno ) );
		return FALSE;
	}

	if ( listen( sock, 1 ) == -1 ) {
		printf( "Unable to listen: %s\n", strerror( errno ) );
		return FALSE;
	}

	recv_sock = accept( sock, NULL, NULL );
	if ( recv_sock == -1 ) {
		printf( "Error while accepting connection: %s\n", strerror( errno ) );
		return FALSE;
	}
	close( sock );

	while ( ( len = read( recv_sock, buf, BUFSIZE ) ) ) {
		if ( len == -1 ) {
			printf( "Error receiving data: %s\n", strerror( errno ) );
			return FALSE;
		}
		write( STDOUT_FILENO, buf, len );
	}

	close( recv_sock );


	return TRUE;
}

int nc_send( void )
{
	int sock;
	struct hostent *host_info;
	struct sockaddr_in addr;
	int result;
	char buf[BUFSIZE];
	ssize_t len;

	sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

	if ( sock == -1 ) {
		printf( "Could not create socket\n" );
		return FALSE;
	}

	host_info = gethostbyname( host );
	if ( host_info == NULL ) {
		printf( "Could not find host %s\n", host );
		return FALSE;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons( port );
	memcpy( &(addr.sin_addr.s_addr), host_info->h_addr, host_info->h_length );
	
	printf( "Connecting to %s\n", inet_ntoa( addr.sin_addr ) );
	
	result = connect( sock, (struct sockaddr *)&addr, sizeof( struct sockaddr_in ) );
	if ( result == -1 ) {
		printf( "Unable to connect: %s\n", strerror( errno ) );
		return FALSE;
	}

	while ( ( len = read( STDIN_FILENO, buf, BUFSIZE ) ) ) {
		if ( len == -1 ) {
			printf( "Error sending data: %s\n", strerror( errno ) );
			return FALSE;
		}
		write( sock, buf, len );
	}

	close( sock );

	return TRUE;
}

int main( int argc, char *argv[] )
{
	int result = FALSE;

	if ( ! parse_cmdline( argc, argv ) ) {
		usage( argv[0] );
		exit( EXIT_FAILURE );
	}

	if ( mode == MODE_GET )
		result = nc_get();
	if ( mode == MODE_SEND )
		result = nc_send();
	
	if ( result ) 
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;
}

