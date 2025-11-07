# HTTP Server

An attempt to write HTTP server in C99.

## Requirement

- Platform: Unix or Unix-like family.
- Compiler: C99 compiler.
- Dependencies: C99 standard library, POSIX library, and OpenSSL library.

## Building

Example building script:

```sh
$ CC="clang" CFLAGS="-DDEBUG -g" LDFLAGS="-g" ./configure
$ gmake
```

## Program Options

| Flag                     | Description                                    |  Default Value  |
| :----------------------- | :--------------------------------------------- | :-------------: |
| `--help`, `-h`           | Display help message.                          |        -        |
| `--worker`               | Set the number of worker thread.               |        1        |
| `--connection`           | Set the maximum number of connections.         |       255       |
| `--buffer`               | Set the size of the buffer.                    |  1048576 (1MB)  |
| `--root-path`            | Set the root directory.                        |      ./www      |
| `--ip4-address`          | Set the IPv4 listen address.                   |     0.0.0.0     |
| `--ip6-address`          | Set the IPv6 listen address (if IPv6 enabled). |       ::        |
| `--ip6-enable`           | Enable the IPv6 address mode.                  |      false      |
| `--tls-enable`           | Enable the TLS mode.                           |      false      |
| `--http-port`            | Set the http listen port.                      |      8080       |
| `--https-port`           | Set the https listen port (if TLS enabled).    |      8443       |
| `--tls-certificate-path` | Set TLS certificate path (if TLS enabled).     | ./fullchain.pem |
| `--tls-private-key-path` | Set TLS private key path (if TLS enabled).     |  ./privkey.pem  |
