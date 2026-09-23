Here's a sketch — a TCP echo server — written strictly against the syntax we've
settled on: colonless declarations, exhaustive `match`, `&` move-in, the `Fd`
handle barrier with synthesized `dispose`, `defer` for cleanup, and the error
trio — `## Try / catch` for failures settled locally, `!` for unwrap-or-panic,
`?` / `??` for `Optional` (absence, not failure). Only `socket.*` and
`runtime.process.*` intrinsics are sketched beyond the core language.

```c
// A TCP echo server: accept forever, echo each received line back, close.
// OS failures are data — Result, not exceptions. Panic stays for bugs.

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

// -- the port: absence (env unset) is Optional absence, not a failure — `?` propagates
portFromEnv func (name string) Optional[uint] = {
  raw := process.env(name)?      // Optional[string]; None → return None
  try p := uint.from(raw)        // a malformed value settles here, not a panic
  return Some(p)

  catch e
  return None
}

// -- bind + listen; failures settle locally with context
newListener func (address string, port uint) Result[Listener] = {
  try fd := socket.listen(address, port)
  Ok(Listener { fd = fd })            // success tail: everything to the catch
  catch e
  return Error("cannot listen on %s{address}:%d{port}: %s{e}")
}

// -- accept one connection; failures are transient, the caller keeps serving
accept func (listener *Listener) Result[Connection] = {
  try fd := socket.accept(listener.fd)     // fd through a view: *Fd
  Ok(Connection { fd = fd, peer = socket.peerName(fd) })
  catch e
  return Error("accept: %s{e}")
}

// -- slurp one line (until \n, or EOF with data)
readLine func (conn *Connection) Result[string] = {
  buf string
  loop {
    try ch := socket.recv(conn.fd)         // view: conn is *Connection
    if ch == '\n' then
      return Ok(buf)
    buf += string.from(ch)
    catch e
    if buf.length > 0 then
      return Ok(buf)                       // EOF with data: deliver what we have
    return Error("connection closed: %s{e}")
  }
}

write func (conn *Connection, data const string) Result[uint] = {
  socket.send(conn.fd, data)               // already Result[uint]: pass through
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
    port    = portFromEnv("PORT") ?? 8080   // None → keep going with the default
  }
  l := newListener(cfg.address, cfg.port)!  // Err → panic (main is exempt)
  serve(&l)                                 // serve borrows a view; we still own the listener
  l.dispose()
}
```
