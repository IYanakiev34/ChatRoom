#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

void usage() {
  char const *usage = R"(
    Usage: client <host> <port>
  )";
  std::cout << usage << std::endl;
  exit(1);
}

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int connect_to_server(std::string const &host, std::string const &port) {
  struct addrinfo hints, *out, *ptr;
  std::memset(&hints, 0, sizeof(struct addrinfo));
  int sockfd;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  int res;

  // Get sockets addresses
  if ((res = getaddrinfo(host.data(), port.data(), &hints, &out)) != 0) {
    std::cerr << "getaddrinfo error: " << gai_strerror(res);
    return -1;
  }

  // Iterate over sockets and connect to one
  for (ptr = out; ptr != nullptr; ptr = ptr->ai_next) {
    if ((sockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) ==
        -1) {
      std::perror("Client socket:");
      continue;
    }

    // try to connect
    if (connect(sockfd, ptr->ai_addr, ptr->ai_addrlen) == -1) {
      std::perror("Client connect:");
      close(sockfd);
      continue;
    }

    break;
  }

  // if ptr == NULL we have not connected
  if (ptr == NULL) {
    std::cerr << "Client: failed to connect\n";
    return -1;
  }

  char addr[INET6_ADDRSTRLEN];
  inet_ntop(ptr->ai_family, get_in_addr(ptr->ai_addr), addr, sizeof(addr));
  std::cout << "Client: connecting to: " << addr << std::endl;

  freeaddrinfo(out); // we do not need it anymore
  return sockfd;
}

void client_loop(int sockfd) {
  std::string msg;
  char buf_rec[1024];
  while (std::getline(std::cin, msg)) {
    ssize_t amount = send(sockfd, msg.data(), sizeof(msg.size()), 0);
    if (amount == -1) {
      std::perror("Client send:");
    }

    ssize_t read = recv(sockfd, buf_rec, 1024, 0);
    if (read <= 0) {
      break;
    }
    std::cout << buf_rec << std::endl;
  }
}

int main(int argc, char **argv) {
  std::cout << "Hello From client!\n";

  if (argc != 3) {
    usage();
  }

  std::string localhost(argv[1]);
  std::string port(argv[2]);

  int sockfd = connect_to_server(localhost, port); // connect to the server
  client_loop(sockfd);                             // loop for messages
  close(sockfd);                                   // close the connection

  return 0;
}
