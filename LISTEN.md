Here's a sketch — a TCP echo server — written strictly against the syntax we've settled on (colonless declarations, Result for fallible ops, match exhaustive, & move-in for ownership, method sugar):

```c
// A TCP echo server: accept forever, echo each received line back, close.
// OS failures are data — Result, not exceptions. Panic stays for bugs.

ServerConfig struct = {
  address string
  port    uint
}

Listener struct = {
  fd int          // owned OS handle; int is Copy, no null ever
}

Connection struct = {
  fd   int
  peer const string   // read-only shared view of the remote address
}

// -- bind + listen
newListener func (address string, port uint) Result[Listener] = {
  fdResult := socket.listen(address, port)   // intrinsic from clib("c")
  return match fdResult {
    Error(e) => Error("cannot listen on %s{address}:%d{port}: %s{e}")
    Ok(fd)   => Ok(Listener { fd = fd })
  }
}

// -- accept one connection; failures here are transient, the caller keeps serving
accept func (listener *Listener) Result[Connection] = {
  return match socket.accept(listener.fd) {
    Error(e) => Error("accept: %s{e}")
    Ok(fd)   => Ok(Connection { fd = fd, peer = socket.peerName(fd) })
  }
}

// -- slurp one line (until \n, or EOF with data)
readLine func (conn *Connection) Result[string] = {
  buf string
  loop {
    match socket.recv(conn.fd) {
      Ok(ch) =>
        if ch == '\n' then
          return Ok(buf)
        buf += string.from(ch)
      Error(e) =>
        if buf.length > 0 then
          return Ok(buf)               // EOF with data: deliver what we have
        return Error("connection closed: %s{e}")
    }
  }
}

write func (conn *Connection, data const string) Result[uint] = {
  socket.send(conn.fd, data)           // returns Result[uint]
}

close func (conn *Connection) = {
  socket.close(conn.fd)
}

// -- one coroutine per connection; `&` moves ownership in
echo func (conn &Connection) = {
  match conn.readLine() {
    Ok(text) =>
      match conn.write(text) {
        Ok(n)    => out.println("echoed %d{n} bytes to %s{conn.peer}")
        Error(e) => out.println("write to %s{conn.peer}: %s{e}")
      }
    Error(e) => out.println("read from %s{conn.peer}: %s{e}")
  }
  conn.close()
}

serve func (listener *Listener) = {
  loop {
    match listener.accept() {
      Ok(conn) => spawn echo(&conn)       // the fd's ownership moves into the coroutine
      Error(e) => out.println("%s{e}")
    }
  }
}

main func () = {
  cfg := ServerConfig { address = "0.0.0.0", port = 8080 }
  match newListener(cfg.address, cfg.port) {
    Ok(l) => serve(&l)
    Error(e) =>
      out.println("fatal: %s{e}")
      panic("server cannot start")
  }
}
```



