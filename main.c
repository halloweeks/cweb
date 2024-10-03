#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <regex.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <signal.h>


#define PORT 8080
#define BUFFER_SIZE 65536 // 64KB
#define MAX_HEADER_SIZE 32768 // 32KB
#define DEFAULT_PAGE "index.html"

int _readline(int fd, char *buffer, size_t size) {
	int index = 0;
	int r = -1;
	char ch;
	
	while (size--) {
		r = read(fd, &ch, 1);
		
		if (r != 1) break; // break if read error or EOF
		
		buffer[index++] = ch;
		
		if (ch == '\n') break;
	}
	
	return (r == -1) ? -1 : index;
}

// Function to get the MIME type based on file extension
const char *get_mime_type(const char *file_path) {
    if (strstr(file_path, ".html") || strstr(file_path, ".htm")) return "text/html";
    if (strstr(file_path, ".css")) return "text/css";
    if (strstr(file_path, ".js")) return "application/javascript";
    if (strstr(file_path, ".png")) return "image/png";
    if (strstr(file_path, ".jpg") || strstr(file_path, ".jpeg")) return "image/jpeg";
    if (strstr(file_path, ".gif")) return "image/gif";
    return "application/octet-stream"; // Default type
}

void _serve_file(int socket, const char *file_path) {
	char buffer[1024];
	char header[1024];
	size_t len;
	
	// Determine the content type
    const char *mime_type = get_mime_type(file_path);
	
	// Prepare response header
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: close\r\n\r\n", mime_type);
	
	// open requested file for reading 
	int file = open(file_path, O_RDONLY);
	
	if (file == -1) {
		strcpy(header, "HTTP/1.1 404 Not Found\r\n");
		strcat(header, "Content-Type: text/html\r\n");
		strcat(header, "Connection: close\r\n");
		strcat(header, "\r\n");
		strcat(header, "<html><body><h1>404 NOT FOUND</h1></body></html>\n");
		
		write(socket, header, strlen(header));
		return;
	}
	
	write(socket, header, strlen(header));
	
	while ((len = read(file, buffer, sizeof(buffer))) > 0) {
		write(socket, buffer, len);
	}
	
	close(file);
	
	return;
}


void process_request(int new_socket) {
	regex_t regex_get, regex_post;
	regex_t data;
	regmatch_t pmatch[3];
	regmatch_t pdata[3];
	
	char filename[100];
	char query[1024];
	char header[1024];
	char sdata[1024];
	
	int REQUEST_METHOD = -1;
	
	
	if (regcomp(&regex_get, "^GET /([^ ?]*)\?([^ ]*) HTTP/1", REG_EXTENDED) != 0) {
		fprintf(stderr, "Could not compile regex\n");
		return;
	}
	
	if (regcomp(&regex_post, "^POST /([^ ]*) HTTP/1", REG_EXTENDED) != 0) {
		fprintf(stderr, "Could not compile regex\n");
		return;
	}
	
	if (regcomp(&data, "\r\n\r\n(.*)", REG_EXTENDED) != 0) {
		fprintf(stderr, "Could not compile regex\n");
		return;
	}
	
	char *buffer = (char*)malloc(BUFFER_SIZE);
	
	if (buffer == NULL) {
		printf("memory allocation failed!\n");
		return;
	}
	
	if (read(new_socket, buffer, BUFFER_SIZE) == -1) {
		printf("failed to read!\n");
		return;
	}
	

	printf("\n\nReceived request:\n%s\n\n", buffer);
	
	if (regexec(&regex_get, buffer, 3, pmatch, 0) == 0) {
		printf("REQUEST METHOD GET!\n");
		REQUEST_METHOD = 0;
	} else if (regexec(&regex_post, buffer, 3, pmatch, 0) == 0) {
		regexec(&data, buffer, 3, pdata, 0);
		
		printf("REQUEST METHOD POST!\n");
		REQUEST_METHOD = 1;
	}
	
	// ONLY GET AND POST REQUEST ALLOWED 
	if (REQUEST_METHOD == -1) {
		strcpy(header, "HTTP/1.1 405 Method Not Allowed\r\n");
		strcat(header, "Content-Type: text/html\r\n");
		strcat(header, "Connection: close\r\n");
		strcat(header, "\r\n");
		strcat(header, "<html><body><h1>405 Method Not Allowed</h1></body></html>");
		
		write(new_socket, header, strlen(header));
		close(new_socket);
		
		regfree(&regex_get);
		regfree(&regex_post);
		free(buffer);
		
		return;
	}
	
	
	printf("REQUEST METHOD: %d\n", REQUEST_METHOD);
	
	
	// Request file
	strncpy(filename, buffer + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
	filename[pmatch[1].rm_eo - pmatch[1].rm_so] = '\0';
	
	printf("file access: %s\n", filename);
	
	if (access(filename, F_OK) == 0) {
        printf("File '%s' exists.\n", filename);
    } else {
        printf("File '%s' does not exist.\n", filename);
    }
	
	if (filename[0] == '\0') {
		_serve_file(new_socket, DEFAULT_PAGE);
	} else {
		_serve_file(new_socket, filename);
	}
	
	strncpy(query, buffer + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so);
	filename[pmatch[1].rm_eo - pmatch[2].rm_so] = '\0';
	
	printf("query: %s\n", query);
	
	// printf("file access 3: %s\n", filename);
	
	
	
	if (REQUEST_METHOD == 1) {
		// post data
		strncpy(sdata, buffer + pdata[1].rm_so, pdata[1].rm_eo - pdata[1].rm_so);
		sdata[pdata[1].rm_eo - pdata[1].rm_so] = '\0';
		printf("post query: %s\n", sdata);
	}
	
	regfree(&regex_get);
	regfree(&regex_post);
	free(buffer);
}

#define DATA_SIZE 52428800 // 50MB


long long file_size(const char *filename) {
	struct stat st;
	
	if (stat(filename, &st) != 0) {
		return -1;
	}
	
	return st.st_size;
}

void method_not_allowed(int socket) {
	char data[1024];
	
	strcpy(data, "HTTP/1.1 405 Method Not Allowed\r\n");
	strcat(data, "Content-Type: text/html\r\n");
	strcat(data, "Connection: close\r\n");
	strcat(data, "\r\n");
	strcat(data, "<html><body><h1>405 Method Not Allowed</h1></body></html>");
		
	write(socket, data, strlen(data));
	close(socket);
}

void _http_response(int socket, int code) {
    char response[1024];

    // Build the response based on the HTTP status code
    switch (code) {
        case 200:
            strcpy(response, "HTTP/1.1 200 OK\r\n");
            break;
        case 201:
            strcpy(response, "HTTP/1.1 201 Created\r\n");
            break;
        case 202:
            strcpy(response, "HTTP/1.1 202 Accepted\r\n");
            break;
        case 204:
            strcpy(response, "HTTP/1.1 204 No Content\r\n");
            break;
        case 400:
            strcpy(response, "HTTP/1.1 400 Bad Request\r\n");
            break;
        case 401:
            strcpy(response, "HTTP/1.1 401 Unauthorized\r\n");
            break;
        case 403:
            strcpy(response, "HTTP/1.1 403 Forbidden\r\n");
            break;
        case 404:
            strcpy(response, "HTTP/1.1 404 Not Found\r\n");
            break;
        case 500:
            strcpy(response, "HTTP/1.1 500 Internal Server Error\r\n");
            break;
        case 503:
            strcpy(response, "HTTP/1.1 503 Service Unavailable\r\n");
            break;
        // Add other cases as needed...
        default:
            strcpy(response, "HTTP/1.1 500 Internal Server Error\r\n");
            break;
    }

    // Send the response
    write(socket, response, strlen(response));
}

int _find(const char *str) {
	for (int i = 0; str[i] != '\0'; i++) {
		if (str[i] == '\r' && str[i+1] == '\n' && str[i+2] == '\r' && str[i+3] == '\n') {
			return i + 4;
		}
	}
	return -1;
}

int _header_length(int socket) {
	char buffer[MAX_HEADER_SIZE]; // maximum 32kb data
	
	int r = recv(socket, buffer, sizeof(buffer), MSG_PEEK);
	
	if (r < 0) return -1;
	
	buffer[r] = '\0';
	
	return _find(buffer);
}

void handle(int socket) {
	regex_t regex;
	regmatch_t matches[3];
	
	char buffer[BUFFER_SIZE];
	
	int len = _header_length(socket);
	
	if (len == -1) {
		_http_response(socket, 400);
		strcpy(buffer, "Content-Type: text/html\r\n");
		strcat(buffer, "Connection: close\r\n");
		strcat(buffer, "\r\n");
		strcat(buffer, "<html><body><h1>Bad request</h1></body></html>");
		write(socket, buffer, strlen(buffer));
		close(socket);
		return;
	}
	
	
	read(socket, buffer, len);
	
	if (regcomp(&regex, "Content-Type: ([^;]+); boundary=([^\\s]+)", REG_EXTENDED) != 0) {
		fprintf(stderr, "Could not compile regex\n");
		return;
	}
    
	
	
	
	
	printf("header size: %d\n", len);
	printf("header data: %s\n", buffer);
	
	memset(buffer, 0, sizeof(buffer));
	
	
	strcpy(buffer, "HTTP/1.1 200 OK\r\n");
	strcat(buffer, "Content-Type: text/html\r\n");
	strcat(buffer, "Connection: close\r\n");
	strcat(buffer, "\r\n");
	strcat(buffer, "<html><body><h1>hello</h1></body></html>");
		
	write(socket, buffer, strlen(buffer));
	
	close(socket);
}



void handle_sigint(int sig) {
    printf("\nServer shutting down gracefully...\n");
    exit(0);
}

int main() {
	signal(SIGINT, handle_sigint); // Graceful shutdown on Ctrl+C
	
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Attach socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // Bind the socket to the address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Start listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", PORT);
    
    while (1) {
        // Accept incoming connections
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        
        // Process the request
        handle(new_socket);
        
        // Close the socket
        close(new_socket);
    }
    
    return 0;
}