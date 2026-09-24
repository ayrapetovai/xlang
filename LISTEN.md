Here's a sketch — a TCP echo server — written strictly against the syntax we've
settled on: colonless declarations, exhaustive `match`, `&` move-in, the `Fd`
handle barrier with synthesized `dispose`, `defer` for cleanup, and the error
trio — `## Try / catch` for failures settled locally, `!` for unwrap-or-panic,
`?` / `??` for `T?` absence (not failure). Only `socket.*` and
`runtime.process.*` intrinsics are sketched beyond the core language.

```c
// A TCP echo server: accept forever, echo each received line back, close.
// OS failures are data — T!, not exceptions. Panic stays for bugs.

// -- the handle barrier: an fd is its own disposable type, not a Copy int
Fd struct = {
  value int
}

// release is the handle's dispose — the only consumer an Fd can have
dispose func (f &Fd) = {
  socket.close(f.value)
}

ServerConfig struct = {
  address string
  port    uint
}

Listener struct = {
  fd Fd           // disposable handle; Listener.dispose is synthesized
}

Connection struct = {
  fd   Fd             // disposable handle; Connection.dispose is synthesized
  peer const string   // read-only shared view of the remote address
}

// -- the port: absence (env unset) is `T?` absence, not a failure — the
// -- checked forms and `??` handle it (C19)
portFromEnv func (name const string) uint? = {
  raw := process.env(name)?      // absent → bare return, absence
  try p := uint.from(raw)        // a malformed value settles here, not a panic
  return p                       // auto-wrap: success
  catch e
  return                         // absence
}

// -- errors are declared kinds; `cause` chains the underlying failure so
// -- `is` tests see through the whole stack
SocketError error = {
  message string
  cause  error
}

// -- bind + listen; failures settle locally with context
newListener func (address const string, port uint) Listener! = {
  try fd := socket.listen(address, port)   // Fd! — unwrapped by the guard
  return Listener { fd = fd }              // auto-wrap: success
  catch e
  return SocketError { message = "cannot listen on %s{address}:%d{port}: %s{e}", cause = e }
}

// -- accept one connection; failures are transient, the caller keeps serving
accept func (listener *Listener) Connection! = {
  try fd := socket.accept(listener.fd)     // fd through a view: *Fd
  peer := socket.peerName(fd)              // read fd first — then move it into the field
  return Connection { fd = fd, peer = peer }
  catch e
  return SocketError { message = "accept: %s{e}", cause = e }
}

// -- slurp one line (until \n, or EOF with data)
readLine func (conn *Connection) string! = {
  buf string
  try {
    loop {
      ch := socket.recv(conn.fd)!      // view: conn is *Connection; fails to the catch
      if ch == '\n' then
        return buf                     // auto-wrap: success
      buf += string.from(ch)
    }
  }
  catch e
  if buf.length > 0 then
    return buf                         // EOF with data: deliver what we have
  return SocketError { message = "connection closed: %s{e}", cause = e }
}

write func (conn *Connection, data const string) uint! = {
  socket.send(conn.fd, data)           // uint! — tail return passes through
}

// -- one coroutine per connection; `&` moves ownership in
// -- one defer, every exit path — including the catch jump
echo func (conn &Connection) = {
  defer conn.dispose()
  try text := conn.readLine()
  try n := conn.write(text)
  out.println("echoed %d{n} bytes to %s{conn.peer}")   // success tail
  catch e
  out.println("echo error: %s{e}")         // conn is disposed here; only e survives
}

serve func (listener *Listener) = {
  loop {
    try conn := listener.accept()
    spawn echo(&conn)                       // success tail: the connection moves in
    catch e
    out.println("accept: %s{e}")            // transient; keep serving
  }
}

main func () = {
  cfg := ServerConfig {
    address = "0.0.0.0"
    port    = portFromEnv("PORT") ?? 8080   // absent → keep going with the default
  }
  l := newListener(cfg.address, cfg.port)!  // failure → panic (main is exempt)
  serve(&l)                                 // serve borrows a view; we still own the listener
  l.dispose()
}
```
