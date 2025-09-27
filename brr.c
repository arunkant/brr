#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>

#define SERVER_ROOT "."

static int PORT = 8080;
char server_root_path[PATH_MAX];

void sigchld_handler(int s) {
	// wait for all dead processes without blocking the parant
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_client(int client_socket) {
	char buffer[1024];
		ssize_t bytes_read;

		bytes_read = read(client_socket, &buffer, sizeof(buffer) - 1);
		if (bytes_read < 0)
		{
			perror("Read failed");
			exit(EXIT_FAILURE);
		}

		buffer[bytes_read] = '\0';

		printf("Client request recieved in child process %d\n", getpid());

		char request_line[1024];
		strncpy(request_line, buffer, sizeof(buffer));

		char *method = strtok(request_line, " ");

		if (method == NULL)
		{
			printf("Invalid HTTP request\n");
			exit(EXIT_FAILURE);
		}

		// get path e.g. '/'
		char *path = strtok(NULL, " ");
		if (path == NULL)
		{
			printf("Invalid HTTP request\n");
			exit(EXIT_FAILURE);
		}

		// Get The protocol
		char *protocol = strtok(NULL, "\r\n");
		if (protocol == NULL)
		{
			printf("Invalid HTTP request\n");
			exit(EXIT_FAILURE);
		}

		// printf("Method: %s, Path: %s, Protocol: %s\n", method, path, protocol);
		char requested_path[PATH_MAX];
		if (strcmp(path, "/") == 0) {
			strcpy(requested_path, "index.html");
		} else {
			strcpy(requested_path, path + 1); // remove leading '/'
		}

		// Security check: Resolve the path and check if it is within the server root
		char full_path[PATH_MAX];
		if (realpath(requested_path, full_path) == NULL || 
			strncmp(full_path, server_root_path, strlen(server_root_path)) != 0) {
			const char *forbidden_response = 
				"HTTP/1.1 403 Forbidden\r\n"
				"\r\n"
				"<h1>403 Forbidden</h1>\r\n";
			write(client_socket, forbidden_response, strlen(forbidden_response));
			close(client_socket);
			return;
		}

		// Determine the Content-Type based on file extension
		char *content_type = "text/plain";
		char *ext = strrchr(full_path, '.');
		if (ext) {
			if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
				content_type = "text/html";
			} else if (strcmp(ext, ".css") == 0) {
				content_type = "text/css";
			} else if (strcmp(ext, ".js") == 0) {
				content_type = "application/javascript";
			} else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
				content_type = "image/jpeg";
			} else if (strcmp(ext, ".png") == 0) {
				content_type = "image/png";
			}
		}

		FILE *file = fopen(full_path, "r");
		if (file == NULL) {
			// File not found, send a 404
			const char *not_found = 
				"HTTP/1.1 404 Not Found\r\n"
				"Content-Type: text/html\r\n"
				"\r\n"
				"<h1>Not Found<h1>";
			write(client_socket, not_found, strlen(not_found));
		} else {
			char ok_header[256];
			sprintf(ok_header,
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: %s\r\n"
				"\r\n", content_type);
			write(client_socket, ok_header, strlen(ok_header));

			// Read the file and send
			char file_buffer[1024];
			size_t bytes_read_from_file;
			while((bytes_read_from_file = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
				write(client_socket, file_buffer, bytes_read_from_file);
			}
			fclose(file);
		}
		close(client_socket);

}

int main()
{
	int server_socket, client_socket;
	struct sockaddr_in server_address, client_address;
	socklen_t client_address_len = sizeof(client_address);
	int opt = 1;

	//identify server root path
	if (realpath(SERVER_ROOT, server_root_path) == NULL) {
		perror("failed to get server root path");
	}

	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0)
	{
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	// Set SO_REUSEADDR to allow the socket to be reused immediately after shutdown
	if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
		perror("setsockopt failed");
		exit(EXIT_FAILURE);
	}

	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(PORT);
	server_address.sin_addr.s_addr = INADDR_ANY;

	if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	if (listen(server_socket, 5) < 0)
	{
		perror("Listen failed");
		exit(EXIT_FAILURE);
	}

	printf("Server listening on port %d...\n", PORT);
	struct sigaction sa;
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	while (1)
	{
		client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
		if (client_socket < 0)
		{
			perror("Accept failed!");
			continue;
		}
		printf("Connection accepted from a client!\n");

		pid_t pid = fork();
		if (pid < 0) {
			perror("fork failed");
			continue;
		}

		if (pid == 0) {
			// Child process
			close(server_socket);
			handle_client(client_socket);
			exit(EXIT_SUCCESS);
		} else {
			// Parant process
			close(client_socket);
		}
		
	}
	close(server_socket); // Unreachable
	return 0;
}