#!/usr/bin/env python3
"""A minimal Icecast-style server: streams an mp3 on loop and injects ICY metadata,
so the station-metadata path can be tested without depending on a public station."""
import socket, threading, time, sys

MP3 = sys.argv[1]
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8123
METAINT = 8192
audio = open(MP3, "rb").read()

def metadata_block(title):
    payload = f"StreamTitle='{title}';".encode("utf-8")
    pad = (16 - len(payload) % 16) % 16
    payload += b"\x00" * pad
    return bytes([len(payload) // 16]) + payload

def serve(conn):
    try:
        request = conn.recv(4096).decode("utf-8", "replace")
        wants_meta = "icy-metadata: 1" in request.lower()
        headers = [
            "ICY 200 OK",
            "icy-name: Test Station FM",
            "icy-genre: Testing",
            "icy-br: 128",
            "content-type: audio/mpeg",
        ]
        if wants_meta:
            headers.append(f"icy-metaint: {METAINT}")
        conn.sendall(("\r\n".join(headers) + "\r\n\r\n").encode())

        pos, since_meta, start = 0, 0, time.time()
        while True:
            chunk = audio[pos:pos + 1024]
            if not chunk:
                pos = 0
                continue
            pos += len(chunk)
            if wants_meta:
                room = METAINT - since_meta
                if len(chunk) >= room:
                    conn.sendall(chunk[:room])
                    # Title changes over time, to prove the UI follows updates.
                    n = int((time.time() - start) // 8)
                    conn.sendall(metadata_block(f"Test Artist {n} - Track {n}"))
                    conn.sendall(chunk[room:])
                    since_meta = len(chunk) - room
                else:
                    conn.sendall(chunk)
                    since_meta += len(chunk)
            else:
                conn.sendall(chunk)
            time.sleep(1024 / 16000.0)   # roughly 128kbps
    except Exception:
        pass
    finally:
        conn.close()

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", PORT))
srv.listen(4)
print(f"icy server on 127.0.0.1:{PORT}", flush=True)
while True:
    c, _ = srv.accept()
    threading.Thread(target=serve, args=(c,), daemon=True).start()
