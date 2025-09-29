#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_ROOT "."
#define MAX_HEADERS 100
#define MAX_HEADER_LEN 1024
#define MAX_PARAMS 20
#define MAX_PARAM_LEN 100

static int PORT = 8080;
char server_root_path[PATH_MAX];

typedef struct {
    char key[50];
    char value[500];
} KeyValuePair;

typedef struct {
    char method[10];
    char path[PATH_MAX];
    char protocol[20];
    KeyValuePair headers[MAX_HEADERS];
    int header_count;
    KeyValuePair params[MAX_PARAMS];
    int param_count;
    char* body;
    size_t body_len;
} HttpRequest;

HttpRequest parse_request(char* request_string) {
    HttpRequest req;
    memset(&req, 0, sizeof(req));

    // 1. parse the request line
    char* request_line_end = strstr(request_string, "\r\n");
    if (request_line_end == NULL)
        return req;
    *request_line_end = '\0';
    sscanf(request_string, "%9s %1023s %19s", req.method, req.path, req.protocol);
	// printf("parse_request path: %s\n", req.path);

    // 2. Parse URL params
    char* param_start = strchr(req.path, '?');
    if (param_start) {
        *param_start = '\0';  // Null terminate the path
        param_start++;        // Move past the ?

        char* token = strtok(param_start, "&");
        while (token != NULL && req.param_count < MAX_PARAMS) {
            char* equal_sign = strchr(param_start, '=');
            if (equal_sign) {
                *equal_sign = '\0';
                strncpy(req.params[req.param_count].key, token, sizeof(req.params[req.param_count].key) - 1);
                req.params[req.param_count].key[sizeof(req.params[req.param_count].key) - 1] = '\0';
                strncpy(req.params[req.param_count].value, equal_sign + 1,
                        sizeof(req.params[req.param_count].value) - 1);
                req.params[req.param_count].value[sizeof(req.params[req.param_count].value) - 1] = '\0';
                req.param_count++;
            }
            token = strtok(NULL, "&");
        }
    }
    // 3. Parse headers
    char* header_line_start = request_line_end + strlen("\r\n");
    char* next_new_line = strstr(header_line_start, "\r\n");
    while (next_new_line && req.header_count < MAX_HEADERS) {
        if (header_line_start == next_new_line) {
            // Found the end of headers and start of body
            req.body = header_line_start + 2;
            req.body_len = strlen(req.body);
            break;
        }
        *next_new_line = '\0';
        char* colon = strchr(header_line_start, ':');
        if (colon) {
            *colon = '\0';
            char* value_start = colon + 1;
            while (*value_start == ' ') {
                value_start++;
            }  // Skip any white space in header value
            strncpy(req.headers[req.header_count].key, header_line_start,
                    sizeof(req.headers[req.header_count].key) - 1);
            strncpy(req.headers[req.header_count].value, value_start, sizeof(req.headers[req.header_count].value) - 1);
            req.headers[req.header_count].key[sizeof(req.headers[req.header_count].key) - 1] = '\0';
            req.headers[req.header_count].value[sizeof(req.headers[req.header_count].value) - 1] = '\0';
            req.header_count++;
        }
        header_line_start = next_new_line + 2;
        next_new_line = strstr(header_line_start, "\r\n");
    }
    return req;
}

void sigchld_handler(int s) {
    // wait for all dead processes without blocking the parant
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_cgi(int client_socket, char* script_path, char* post_data) {
    int pipefd[2];

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // this is child process
        close(pipefd[1]);  // close write end of pipe

        // Redirect stdin to the read end of the pipe
        dup2(pipefd[0], STDIN_FILENO);
        // Redirect stdout to client socket
        dup2(client_socket, STDOUT_FILENO);

        // Executing the CGI script
        char* argv[] = {script_path, NULL};
        char* envp[] = {NULL};
        execve(script_path, argv, envp);

        // If execve returns then it faild
        perror("execve failed");
        exit(EXIT_FAILURE);
    } else {
        // This is parant process
        close(pipefd[0]);  // close read end of the pipe
        if (post_data) {
            write(pipefd[1], post_data, strlen(post_data));
        }
        close(pipefd[1]);  // Close to signal end of data
        waitpid(pid, NULL, 0);
    }
}

void handle_client(int client_socket) {
    char buffer[1024];
    ssize_t bytes_read;

    bytes_read = read(client_socket, &buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("Read failed");
        exit(EXIT_FAILURE);
    }

    buffer[bytes_read] = '\0';

    printf("Client request recieved in child process %d\n", getpid());

    HttpRequest request = parse_request(buffer);
	// printf("in handle_client: %s, %s, %s\n", request.method, request.path, request.protocol);

    // printf("Method: %s, Path: %s, Protocol: %s\n", method, path, protocol);
    char requested_path[PATH_MAX];
    if (strcmp(request.path, "/") == 0) {
        strcpy(requested_path, "index.html");
    } else {
        strcpy(requested_path, request.path + 1);  // remove leading '/'
    }

    // Security check: Resolve the path and check if it is within the server root
    char full_path[PATH_MAX];
    if (realpath(requested_path, full_path) == NULL ||
        strncmp(full_path, server_root_path, strlen(server_root_path)) != 0) {
        const char* forbidden_response =
            "HTTP/1.1 403 Forbidden\r\n"
            "\r\n"
            "<h1>403 Forbidden</h1>\r\n";
        write(client_socket, forbidden_response, strlen(forbidden_response));
        close(client_socket);
        return;
    }

    

    // Determine the Content-Type based on file extensionf
    char* content_type = "text/plain";
    char* ext = strrchr(full_path, '.');
    // Handle CGI requests
    if (ext && strcmp(ext, ".cgi") == 0) {
		printf("hello here");
        handle_cgi(client_socket, full_path, request.body);
        return;
    }
    // Handle normal file serving
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

    FILE* file = fopen(full_path, "r");
    if (file == NULL) {
        // File not found, send a 404
        const char* not_found =
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
                "\r\n",
                content_type);
        write(client_socket, ok_header, strlen(ok_header));

        // Read the file and send
        char file_buffer[1024];
        size_t bytes_read_from_file;
        while ((bytes_read_from_file = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
            write(client_socket, file_buffer, bytes_read_from_file);
        }
        fclose(file);
    }
    close(client_socket);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_len = sizeof(client_address);
    int opt = 1;

    //identify server root path
    if (realpath(SERVER_ROOT, server_root_path) == NULL) {
        perror("failed to get server root path");
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set SO_REUSEADDR to allow the socket to be reused immediately after shutdown
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0) {
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

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_address, &client_address_len);
        if (client_socket < 0) {
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
    close(server_socket);  // Unreachable
    return 0;
}