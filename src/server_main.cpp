#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void usage() {
  char const *usage = R"(
    Usage: server <port>
  )";
  std::cout << usage << std::endl;
  exit(1);
}

void *get_addr_in(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

static constexpr uint32_t BACKLOG_SIZE = 20;
static constexpr uint32_t MAX_CONNECTIONS = 128;

void add_fd(struct pollfd *fds, int new_fd, size_t &fd_size,
            size_t &fd_capacity) {
  if (fd_size == MAX_CONNECTIONS) {
    std::cerr << "Cannot accept more connections\n";
    return;
  }

  if (fd_size == fd_capacity) {
    fd_capacity *= 2;
    fds = (struct pollfd *)realloc(fds, fd_capacity * sizeof(struct pollfd));
  }

  fds[fd_size].fd = new_fd;
  fds[fd_size].events = POLLIN;
  ++fd_size;
}

void del_fd(struct pollfd *fds, size_t &idx, size_t &fds_size) {
  fds[idx] = fds[fds_size - 1];
  fds_size--;
}

int get_listener_socket(std::string const &port) {
  struct addrinfo hints, *out, *ptr;
  int res;
  int listenerfd;
  int yes = 1;

  // Buffers for client name and information
  std::memset(&hints, 0, sizeof(addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if ((res = getaddrinfo(NULL, port.data(), &hints, &out)) != 0) {
    std::cerr << "Server getaddrinfo error: " << gai_strerror(res) << std::endl;
    exit(1);
  }

  // iterate over socket bind points
  for (ptr = out; ptr != NULL; ptr = ptr->ai_next) {
    if ((listenerfd = socket(ptr->ai_family, ptr->ai_socktype,
                             ptr->ai_protocol)) == -1) {
      std::perror("Server socket:");
      continue;
    }

    // Set socket options to be resuable
    if (setsockopt(listenerfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes,
                   sizeof(int)) == -1) {
      std::perror("Server setsockopt:");
      exit(1);
    }

    if (bind(listenerfd, ptr->ai_addr, ptr->ai_addrlen) == -1) {
      std::perror("Server bind:");
      close(listenerfd);
      continue;
    }

    break;
  }

  freeaddrinfo(out);
  if (ptr == NULL) {
    std::cerr << "Server failed to bind\n";
    return -1;
  }

  if (listen(listenerfd, BACKLOG_SIZE) == -1) {
    std::perror("Server listen:");
    close(listenerfd);
    return -1;
  }

  return listenerfd;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    usage();
  }

  struct sockaddr_storage their_addr;
  socklen_t sin_size;

  int listenerfd;

  // Buffers for client name and information
  char client_addr[INET6_ADDRSTRLEN];
  std::array<char, 1024> buffer;

  size_t pollfd_capacity = 8;
  size_t pollfd_size = 0;
  struct pollfd *poll_fds =
      (struct pollfd *)malloc(sizeof(struct pollfd) * pollfd_capacity);

  listenerfd = get_listener_socket(argv[1]);
  if (listenerfd == -1) {
    exit(1);
  }

  add_fd(poll_fds, listenerfd, pollfd_size, pollfd_capacity);
  std::cout << "Server: waiting for incomming connections...\n";

  for (;;) {
    int poll_count = poll(poll_fds, pollfd_size, -1);
    if (poll_count == -1) {
      std::perror("poll");
      exit(1);
    }

    for (size_t idx = 0; idx != pollfd_size; ++idx) {
      // Check if the fd is ready to read
      if (poll_fds[idx].revents & POLLIN) {

        // check if the listener is ready to accept connections
        if (poll_fds[idx].fd == listenerfd) {
          sin_size = sizeof(their_addr);
          int new_fd =
              accept(listenerfd, (struct sockaddr *)&their_addr, &sin_size);
          if (new_fd == -1) {
            std::perror("accept");
          } else {
            add_fd(poll_fds, new_fd, pollfd_size, pollfd_capacity);
            inet_ntop(their_addr.ss_family,
                      get_addr_in((struct sockaddr *)&their_addr), client_addr,
                      sizeof(client_addr));
            std::cout << "Server: got connection from " << client_addr
                      << std::endl;
          }
        } else { // We are ready to read from a client
          int sender_fd = poll_fds[idx].fd;
          ssize_t nbytes = recv(sender_fd, buffer.data(), buffer.size(), 0);

          if (nbytes <= 0) {
            if (nbytes ==
                0) { // client closed the connection so remove it from the fds
              std::cout << "pollserver: socket " << sender_fd << " hung up\n";
            } else {
              std::perror("recv");
            }
            shutdown(sender_fd, SHUT_RDWR);
            close(sender_fd);                   // close the file descriptor
            del_fd(poll_fds, idx, pollfd_size); // delete the fd from the array

          } else { // END OF CASE WHEN <= 0 bytes read Start of case when we
                   // read bytes
            // We got some data from a client and send it to every other client
            std::cout << "We got: " << buffer.data() << std::endl;

            for (size_t j = 0; j != pollfd_size; ++j) {
              int dest_fd = poll_fds[j].fd;
              // Except for server and the client that sent the message
              if (dest_fd != listenerfd && dest_fd != sender_fd) {
                if (send(dest_fd, buffer.data(), buffer.size(), 0) == -1) {
                  std::perror("send");
                }
              }
            } // END OF LOOP FOR SENDING MESSAGE
          }
        } // END OF Client case
      }   // END OF CASE when we have descriptor to read from
    }     // END OF FOR LOOP over fds
  }       // END OF server loop

  return 0;
}
